use std::{
    net::{SocketAddr, SocketAddrV4},
    str::FromStr,
    sync::Arc,
};

use clap::Parser;
use color_eyre::eyre::{bail, ContextCompat, Report};
use futures_util::{stream::StreamExt, SinkExt};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream},
};

use tokio_tungstenite::tungstenite::{handshake::server::Request, Message};

const READ_BUFFER_SIZE: usize = 8 * 1024; // 8 kb
const LIVE_GAME_PORT: u16 = 3791;
const BUILDER_TEST_GAME_PORT: u16 = 4802;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum BuilderApiOperation {
    Login,
    Manifest,
    Status,
    Stage,
    Activate,
    Rollback,
    Logout,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct BuilderApiRoute {
    method: &'static str,
    path: &'static str,
    operation: BuilderApiOperation,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderLoginRequest {
    account: String,
    password: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderSessionResponse {
    token: String,
    account_id: String,
    expires_at_epoch_seconds: i64,
    immortal_character_names: Vec<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderManifestResponse {
    manifest_checksum: String,
    generated_typings_version: String,
    trigger_catalog_revision: i32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderPackageRef {
    package_id: String,
    zone: i32,
    host: String,
    vnum: i32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderStatusRequest {
    package_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderStageRequest {
    base_live_checksum: String,
    package_json: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderActivateRequest {
    package_id: String,
    staged_digest: String,
    base_live_checksum: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderRollbackRequest {
    package_id: String,
    target_live_checksum: String,
    reason: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderPublishResponse {
    ok: bool,
    reason_code: String,
    audit_id: Option<String>,
    package: Option<BuilderPackageRef>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderLogoutResponse {
    ok: bool,
}

const BUILDER_API_ROUTES: [BuilderApiRoute; 7] = [
    BuilderApiRoute {
        method: "POST",
        path: "/api/builder/login",
        operation: BuilderApiOperation::Login,
    },
    BuilderApiRoute {
        method: "GET",
        path: "/api/builder/js/manifest",
        operation: BuilderApiOperation::Manifest,
    },
    BuilderApiRoute {
        method: "POST",
        path: "/api/builder/js/status",
        operation: BuilderApiOperation::Status,
    },
    BuilderApiRoute {
        method: "POST",
        path: "/api/builder/js/stage",
        operation: BuilderApiOperation::Stage,
    },
    BuilderApiRoute {
        method: "POST",
        path: "/api/builder/js/activate",
        operation: BuilderApiOperation::Activate,
    },
    BuilderApiRoute {
        method: "POST",
        path: "/api/builder/js/rollback",
        operation: BuilderApiOperation::Rollback,
    },
    BuilderApiRoute {
        method: "POST",
        path: "/api/builder/logout",
        operation: BuilderApiOperation::Logout,
    },
];

fn builder_api_route(method: &str, path: &str) -> Option<BuilderApiRoute> {
    BUILDER_API_ROUTES
        .iter()
        .copied()
        .find(|route| route.method == method && route.path == path)
}

fn builder_publish_target(game: &GameAddr) -> Result<(), Report> {
    let host = game.hostname.as_str();
    if host != "127.0.0.1" && host != "localhost" {
        bail!("invalid BuilderClient publish target");
    }
    if game.port == LIVE_GAME_PORT {
        bail!("invalid BuilderClient publish target");
    }
    if game.port != BUILDER_TEST_GAME_PORT {
        bail!("invalid BuilderClient publish target");
    }
    Ok(())
}

#[derive(Clone, Debug)]
struct GameAddr {
    hostname: Arc<String>,
    port: u16,
}

impl GameAddr {
    async fn connect(&self) -> Result<TcpStream, Report> {
        Ok(TcpStream::connect((self.hostname.as_str(), self.port)).await?)
    }
}

impl FromStr for GameAddr {
    type Err = Report;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let (hostname, port) = s.split_once(':').context("Missing port")?;

        Ok(Self {
            hostname: Arc::new(String::from(hostname)),
            port: port.parse()?,
        })
    }
}

/// Simple program to greet a person
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    #[arg(short, long, default_value = "127.0.0.1:1024")]
    /// Address of the game
    game: GameAddr,

    #[arg(long, default_value = "127.0.0.1:4802")]
    /// Address of the test game used by the BuilderClient API
    builder_game: GameAddr,

    #[arg(short, long, default_value = "0.0.0.0:3791")]
    /// TCP address to listen
    listen: SocketAddrV4,

    #[arg(short, long, default_value = "0.0.0.0:8080")]
    /// WebSocket address to listen
    websocket: SocketAddrV4,

    #[arg(short, long)]
    /// Get the connecting IP from the Cloudflare header
    cloudflare: bool,
}

async fn handle_tcp(game: GameAddr, mut stream: TcpStream, addr: SocketAddr) -> Result<(), Report> {
    let addr = match addr {
        SocketAddr::V4(addr) => addr,
        SocketAddr::V6(addr) => bail!("Unexpected IPv6: {addr}"),
    };

    // Connect to the game
    let mut game = game.connect().await?;

    // Write the proxy header
    game.write_u32(u32::from(*addr.ip())).await?;

    // Start proxying
    tokio::io::copy_bidirectional(&mut stream, &mut game).await?;

    Ok(())
}

async fn tcp_server(game: GameAddr, listener: TcpListener) -> Result<(), Report> {
    loop {
        let (stream, addr) = listener.accept().await?;
        let game = game.clone();

        log::debug!("Received TCP connection on {addr}");

        tokio::spawn(async move {
            if let Err(err) = handle_tcp(game, stream, addr).await {
                log::error!("{err}");
            }
        });
    }
}

async fn handle_ws(
    game: GameAddr,
    stream: TcpStream,
    addr: SocketAddr,
    cloudflare: bool,
) -> Result<(), Report> {
    let mut addr = match addr {
        SocketAddr::V4(addr) => *addr.ip(),
        SocketAddr::V6(addr) => bail!("Unexpected IPv6: {addr}"),
    };

    let mut ws = tokio_tungstenite::accept_hdr_async(stream, |req: &Request, res| {
        if let Some(header) = cloudflare
            .then_some(req.headers())
            .and_then(|h| h.get("CF-Connecting-IP"))
            .and_then(|h| h.to_str().ok())
            .and_then(|h| h.parse().ok())
        {
            addr = header;
        }

        Ok(res)
    })
    .await?;

    let mut game = game.connect().await?;
    let mut buf = [0; READ_BUFFER_SIZE];

    // Write the proxy header
    game.write_u32(addr.into()).await?;

    // Start proxying
    loop {
        tokio::select! {
            msg = ws.next() => {
                let msg = if let Some(msg) = msg {
                    msg?
                } else {
                    break;
                };

                match msg {
                    Message::Text(msg) => game.write_all(msg.as_bytes()).await?,
                    Message::Binary(msg) => game.write_all(&msg).await?,
                    Message::Close(_) => break,
                    _ => continue,
                }
            },
            read = game.read(&mut buf) => {
                let read = read?;

                if read > 0 {
                    ws.send(Message::Binary(Vec::from(&buf[..read]))).await?;
                }
            }
        }
    }

    Ok(())
}

async fn ws_server(game: GameAddr, listener: TcpListener, cloudflare: bool) -> Result<(), Report> {
    loop {
        let (stream, addr) = listener.accept().await?;
        let game = game.clone();

        log::debug!("Received websocket connection on {addr}");

        tokio::spawn(async move {
            if let Err(err) = handle_ws(game, stream, addr, cloudflare).await {
                log::error!("{err}");
            }
        });
    }
}

#[tokio::main]
async fn main() -> Result<(), Report> {
    env_logger::init();
    color_eyre::install()?;

    let args = Args::parse();
    builder_publish_target(&args.builder_game)?;
    let tcp = TcpListener::bind(args.listen).await?;
    let ws = TcpListener::bind(args.websocket).await?;

    if let Ok(addr) = tcp.local_addr() {
        log::info!("Listening for TCP connections on {}", addr);
    }

    if let Ok(addr) = ws.local_addr() {
        log::info!("Listening for WebSocket connections on {}", addr);
    }

    let tcp = tokio::spawn(tcp_server(args.game.clone(), tcp));
    let ws = tokio::spawn(ws_server(args.game, ws, args.cloudflare));

    tokio::select! {
        res = tcp => res??,
        res = ws => res??,
    };

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    fn game_addr(hostname: &str, port: u16) -> GameAddr {
        GameAddr {
            hostname: Arc::new(String::from(hostname)),
            port,
        }
    }

    #[test]
    fn builder_api_routes_cover_auth_manifest_publish_and_logout() {
        let expected = [
            ("POST", "/api/builder/login", BuilderApiOperation::Login),
            (
                "GET",
                "/api/builder/js/manifest",
                BuilderApiOperation::Manifest,
            ),
            (
                "POST",
                "/api/builder/js/status",
                BuilderApiOperation::Status,
            ),
            ("POST", "/api/builder/js/stage", BuilderApiOperation::Stage),
            (
                "POST",
                "/api/builder/js/activate",
                BuilderApiOperation::Activate,
            ),
            (
                "POST",
                "/api/builder/js/rollback",
                BuilderApiOperation::Rollback,
            ),
            ("POST", "/api/builder/logout", BuilderApiOperation::Logout),
        ];

        for (method, path, operation) in expected {
            let route = builder_api_route(method, path).expect("route should exist");
            assert_eq!(operation, route.operation);
        }
    }

    #[test]
    fn builder_api_routes_reject_wrong_method_or_unknown_path() {
        assert_eq!(None, builder_api_route("GET", "/api/builder/login"));
        assert_eq!(None, builder_api_route("POST", "/api/builder/js/manifest"));
        assert_eq!(None, builder_api_route("POST", "/api/builder/js/export"));
        assert_eq!(None, builder_api_route("POST", "/api/js-scripts/stage"));
        assert_eq!(None, builder_api_route("post", "/api/builder/js/stage"));
        assert_eq!(None, builder_api_route("POST", "/api/builder/js/stage/"));
        assert_eq!(
            None,
            builder_api_route("POST", "/api/builder/js/stage/extra")
        );
        assert_eq!(
            None,
            builder_api_route("GET", "/api/builder/js/manifest?zone=30")
        );
    }

    #[test]
    fn builder_api_routes_are_unique_by_method_and_path() {
        let mut seen = HashSet::new();

        for route in BUILDER_API_ROUTES {
            assert!(
                seen.insert((route.method, route.path)),
                "duplicate route {} {}",
                route.method,
                route.path
            );
        }
    }

    #[test]
    fn builder_api_contract_dtos_capture_client_and_server_fields() {
        let login = BuilderLoginRequest {
            account: String::from("builder@example.com"),
            password: String::from("secret"),
        };
        let session = BuilderSessionResponse {
            token: String::from("token:builder"),
            account_id: String::from("account:builder"),
            expires_at_epoch_seconds: 200,
            immortal_character_names: vec![String::from("Builderone")],
        };
        let manifest = BuilderManifestResponse {
            manifest_checksum: String::from("manifest:sha256"),
            generated_typings_version: String::from("typings:v1"),
            trigger_catalog_revision: 7,
        };
        let package = BuilderPackageRef {
            package_id: String::from("js:30:character:3001"),
            zone: 30,
            host: String::from("character"),
            vnum: 3001,
        };
        let status = BuilderStatusRequest {
            package_id: package.package_id.clone(),
        };
        let stage = BuilderStageRequest {
            base_live_checksum: String::from("live:old"),
            package_json: String::from("{\"packageId\":\"client-local\"}"),
        };
        let activate = BuilderActivateRequest {
            package_id: package.package_id.clone(),
            staged_digest: String::from("sha256:staged"),
            base_live_checksum: String::from("live:old"),
        };
        let rollback = BuilderRollbackRequest {
            package_id: package.package_id.clone(),
            target_live_checksum: String::from("live:old"),
            reason: Some(String::from("builder rollback")),
        };
        let publish = BuilderPublishResponse {
            ok: true,
            reason_code: String::from("stage.accepted"),
            audit_id: Some(String::from("audit:proxy")),
            package: Some(package),
        };
        let logout = BuilderLogoutResponse { ok: true };

        assert_eq!("builder@example.com", login.account);
        assert_eq!("account:builder", session.account_id);
        assert_eq!("manifest:sha256", manifest.manifest_checksum);
        assert_eq!("js:30:character:3001", status.package_id);
        assert_eq!("live:old", stage.base_live_checksum);
        assert_eq!("sha256:staged", activate.staged_digest);
        assert_eq!(Some(String::from("builder rollback")), rollback.reason);
        assert_eq!(Some(String::from("audit:proxy")), publish.audit_id);
        assert!(logout.ok);
    }

    #[test]
    fn builder_publish_target_allows_test_server_port() {
        assert!(builder_publish_target(&game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT)).is_ok());
        assert!(builder_publish_target(&game_addr("localhost", BUILDER_TEST_GAME_PORT)).is_ok());
    }

    #[test]
    fn builder_publish_target_rejects_live_game_port() {
        let error = builder_publish_target(&game_addr("127.0.0.1", LIVE_GAME_PORT))
            .expect_err("live game port must be rejected")
            .to_string();

        assert!(error.contains("invalid BuilderClient publish target"));
    }

    #[test]
    fn builder_publish_target_rejects_unexpected_ports() {
        let error = builder_publish_target(&game_addr("127.0.0.1", 1024))
            .expect_err("unexpected game port must be rejected")
            .to_string();

        assert!(error.contains("invalid BuilderClient publish target"));
    }

    #[test]
    fn builder_publish_target_rejects_non_localhost_targets() {
        let error = builder_publish_target(&game_addr("example.com", BUILDER_TEST_GAME_PORT))
            .expect_err("remote builder target must be rejected")
            .to_string();

        assert!(error.contains("invalid BuilderClient publish target"));
    }

    #[test]
    fn builder_game_cli_defaults_to_test_server_port() {
        let args = Args::parse_from(["proxy"]);

        assert_eq!(BUILDER_TEST_GAME_PORT, args.builder_game.port);
        assert!(builder_publish_target(&args.builder_game).is_ok());
    }

    #[test]
    fn builder_game_cli_rejects_live_port() {
        let args = Args::parse_from(["proxy", "--builder-game", "127.0.0.1:3791"]);

        assert!(builder_publish_target(&args.builder_game).is_err());
    }
}
