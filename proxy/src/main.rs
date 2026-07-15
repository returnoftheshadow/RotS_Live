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
const MAX_BUILDER_API_REQUEST_BYTES: usize = 256 * 1024;

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

impl BuilderApiOperation {
    fn requires_session(self) -> bool {
        !matches!(self, Self::Login | Self::Manifest)
    }

    fn requires_json_body(self) -> bool {
        !matches!(self, Self::Manifest)
    }
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
    package: BuilderStagePackage,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderStagePackage {
    package_id: String,
    zone: i32,
    host: String,
    vnum: i32,
    provenance: BuilderPackageProvenance,
    compiled_javascript: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderPackageProvenance {
    repository: String,
    branch: String,
    commit: String,
    dirty: bool,
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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum BuilderApiPreflightCode {
    Accepted,
    NotFound,
    UnsupportedMediaType,
    RequestTooLarge,
    MissingSession,
    InvalidPublishTarget,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderApiPreflightRequest<'a> {
    method: &'a str,
    path: &'a str,
    content_type: Option<&'a str>,
    body_bytes: usize,
    bearer_token: Option<&'a str>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderApiPreflightResult {
    code: BuilderApiPreflightCode,
    route: Option<BuilderApiRoute>,
    http_status: u16,
    reason_code: &'static str,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderApiHttpRequest<'a> {
    method: &'a str,
    path: &'a str,
    content_type: Option<&'a str>,
    bearer_token: Option<&'a str>,
    body: &'a [u8],
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

fn is_json_content_type(content_type: Option<&str>) -> bool {
    let Some(content_type) = content_type else {
        return false;
    };
    let mut parts = content_type.split(';');
    let media_type = parts.next().unwrap_or_default().trim();
    if !media_type.eq_ignore_ascii_case("application/json") {
        return false;
    }
    parts.all(|part| {
        let part = part.trim();
        part.is_empty()
            || part
                .split_once('=')
                .map(|(name, _)| !name.trim().is_empty())
                .unwrap_or(false)
    })
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

fn builder_api_preflight(
    request: &BuilderApiPreflightRequest<'_>,
    builder_game: &GameAddr,
) -> BuilderApiPreflightResult {
    let Some(route) = builder_api_route(request.method, request.path) else {
        return BuilderApiPreflightResult {
            code: BuilderApiPreflightCode::NotFound,
            route: None,
            http_status: 404,
            reason_code: "builder.not-found",
        };
    };

    if builder_publish_target(builder_game).is_err() {
        return BuilderApiPreflightResult {
            code: BuilderApiPreflightCode::InvalidPublishTarget,
            route: Some(route),
            http_status: 503,
            reason_code: "builder.invalid-target",
        };
    }

    if request.body_bytes > MAX_BUILDER_API_REQUEST_BYTES {
        return BuilderApiPreflightResult {
            code: BuilderApiPreflightCode::RequestTooLarge,
            route: Some(route),
            http_status: 413,
            reason_code: "builder.request-too-large",
        };
    }

    if route.operation.requires_json_body() && !is_json_content_type(request.content_type) {
        return BuilderApiPreflightResult {
            code: BuilderApiPreflightCode::UnsupportedMediaType,
            route: Some(route),
            http_status: 415,
            reason_code: "builder.unsupported-media-type",
        };
    }

    if route.operation.requires_session()
        && request
            .bearer_token
            .map(str::trim)
            .filter(|token| !token.is_empty())
            .is_none()
    {
        return BuilderApiPreflightResult {
            code: BuilderApiPreflightCode::MissingSession,
            route: Some(route),
            http_status: 401,
            reason_code: "builder.missing-session",
        };
    }

    BuilderApiPreflightResult {
        code: BuilderApiPreflightCode::Accepted,
        route: Some(route),
        http_status: 200,
        reason_code: "builder.accepted",
    }
}

fn builder_api_handle_http_request(
    request: &BuilderApiHttpRequest<'_>,
    builder_game: &GameAddr,
) -> BuilderApiPreflightResult {
    let preflight = BuilderApiPreflightRequest {
        method: request.method,
        path: request.path,
        content_type: request.content_type,
        body_bytes: request.body.len(),
        bearer_token: request.bearer_token,
    };
    builder_api_preflight(&preflight, builder_game)
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
            package: BuilderStagePackage {
                package_id: String::from("client-local"),
                zone: 30,
                host: String::from("character"),
                vnum: 3001,
                provenance: BuilderPackageProvenance {
                    repository: String::from("scripts"),
                    branch: String::from("main"),
                    commit: String::from("abc123"),
                    dirty: false,
                },
                compiled_javascript: String::from("function onEnter(ctx) {}"),
            },
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
        assert_eq!("abc123", stage.package.provenance.commit);
        assert_eq!(
            "function onEnter(ctx) {}",
            stage.package.compiled_javascript
        );
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
        for host in [
            "example.com",
            "192.168.1.50",
            "0.0.0.0",
            "::1",
            "LOCALHOST",
            "localhost.",
            "127.0.0.1.evil.example",
        ] {
            let error = builder_publish_target(&game_addr(host, BUILDER_TEST_GAME_PORT))
                .expect_err("remote builder target must be rejected")
                .to_string();

            assert!(error.contains("invalid BuilderClient publish target"));
        }
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

    #[test]
    fn builder_api_preflight_accepts_login_manifest_and_authed_publish_routes() {
        let game = game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT);
        let login = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/login",
            content_type: Some("Application/Json ; charset=UTF-8"),
            bearer_token: None,
            body: &[b'x'; 128],
        };
        let manifest = BuilderApiHttpRequest {
            method: "GET",
            path: "/api/builder/js/manifest",
            content_type: None,
            bearer_token: None,
            body: &[],
        };
        let stage = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json;charset=utf-8; profile=builder"),
            bearer_token: Some("token:builder"),
            body: &[b'x'; 4096],
        };

        assert_eq!(
            BuilderApiPreflightCode::Accepted,
            builder_api_handle_http_request(&login, &game).code
        );
        assert_eq!(
            BuilderApiPreflightCode::Accepted,
            builder_api_handle_http_request(&manifest, &game).code
        );
        assert_eq!(
            BuilderApiPreflightCode::Accepted,
            builder_api_handle_http_request(&stage, &game).code
        );
    }

    #[test]
    fn builder_api_preflight_rejects_unknown_routes_before_target_details() {
        let live_game = game_addr("127.0.0.1", LIVE_GAME_PORT);
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage/extra",
            content_type: Some("application/json"),
            bearer_token: Some("token:builder"),
            body: &[b'x'; 128],
        };
        let result = builder_api_handle_http_request(&request, &live_game);

        assert_eq!(BuilderApiPreflightCode::NotFound, result.code);
        assert_eq!(404, result.http_status);
        assert_eq!("builder.not-found", result.reason_code);
        assert_eq!(None, result.route);
    }

    #[test]
    fn builder_api_preflight_rejects_invalid_publish_target_generically() {
        let live_game = game_addr("127.0.0.1", LIVE_GAME_PORT);
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json"),
            bearer_token: Some("token:builder"),
            body: &[b'x'; 128],
        };
        let result = builder_api_handle_http_request(&request, &live_game);

        assert_eq!(BuilderApiPreflightCode::InvalidPublishTarget, result.code);
        assert_eq!(503, result.http_status);
        assert_eq!("builder.invalid-target", result.reason_code);
    }

    #[test]
    fn builder_api_preflight_rejects_oversized_request_before_auth_checks() {
        let game = game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT);
        let body = vec![b'x'; MAX_BUILDER_API_REQUEST_BYTES + 1];
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json"),
            bearer_token: None,
            body: &body,
        };
        let result = builder_api_handle_http_request(&request, &game);

        assert_eq!(BuilderApiPreflightCode::RequestTooLarge, result.code);
        assert_eq!(413, result.http_status);
        assert_eq!("builder.request-too-large", result.reason_code);
    }

    #[test]
    fn builder_api_preflight_allows_exact_request_size_limit() {
        let game = game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT);
        let body = vec![b'x'; MAX_BUILDER_API_REQUEST_BYTES];
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json"),
            bearer_token: Some("token:builder"),
            body: &body,
        };
        let result = builder_api_handle_http_request(&request, &game);

        assert_eq!(BuilderApiPreflightCode::Accepted, result.code);
    }

    #[test]
    fn builder_api_preflight_allows_empty_json_body_through_preflight() {
        let game = game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT);
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json"),
            bearer_token: Some("token:builder"),
            body: &[],
        };
        let result = builder_api_handle_http_request(&request, &game);

        assert_eq!(BuilderApiPreflightCode::Accepted, result.code);
    }

    #[test]
    fn builder_api_preflight_rejects_missing_json_for_body_routes() {
        let game = game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT);
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("text/plain"),
            bearer_token: Some("token:builder"),
            body: &[b'x'; 128],
        };
        let result = builder_api_handle_http_request(&request, &game);

        assert_eq!(BuilderApiPreflightCode::UnsupportedMediaType, result.code);
        assert_eq!(415, result.http_status);
        assert_eq!("builder.unsupported-media-type", result.reason_code);
    }

    #[test]
    fn builder_api_preflight_rejects_missing_session_for_publish_routes() {
        let game = game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT);
        for path in [
            "/api/builder/js/status",
            "/api/builder/js/stage",
            "/api/builder/js/activate",
            "/api/builder/js/rollback",
            "/api/builder/logout",
        ] {
            for token in [None, Some(" ")] {
                let request = BuilderApiHttpRequest {
                    method: "POST",
                    path,
                    content_type: Some("application/json"),
                    bearer_token: token,
                    body: &[b'x'; 128],
                };
                let result = builder_api_handle_http_request(&request, &game);

                assert_eq!(BuilderApiPreflightCode::MissingSession, result.code);
                assert_eq!(401, result.http_status);
                assert_eq!("builder.missing-session", result.reason_code);
            }
        }
    }

    #[test]
    fn builder_api_preflight_accepts_trimmed_session_token_presence() {
        let game = game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT);
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/status",
            content_type: Some("application/json"),
            bearer_token: Some(" token:builder "),
            body: &[b'x'; 128],
        };
        let result = builder_api_handle_http_request(&request, &game);

        assert_eq!(BuilderApiPreflightCode::Accepted, result.code);
    }
}
