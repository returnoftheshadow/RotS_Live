use std::{
    collections::HashMap,
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
    time::{timeout, Duration},
};

use tokio_tungstenite::tungstenite::{handshake::server::Request, Message};

const READ_BUFFER_SIZE: usize = 8 * 1024; // 8 kb
const LIVE_GAME_PORT: u16 = 3791;
const BUILDER_TEST_GAME_PORT: u16 = 4802;
const MAX_BUILDER_API_REQUEST_BYTES: usize = 256 * 1024;
const MAX_BUILDER_API_RESPONSE_BYTES: usize = 256 * 1024;
const MAX_BUILDER_API_HEADER_BYTES: usize = 16 * 1024;
const BUILDER_API_READ_TIMEOUT: Duration = Duration::from_secs(5);
const BUILDER_GAME_FORWARD_TIMEOUT: Duration = Duration::from_secs(5);
const BUILDER_INTERNAL_TRUST_HEADER: &str = "x-rots-builder-proxy-secret";
const BUILDER_INTERNAL_SECRET_ENV: &str = "ROTS_BUILDER_PROXY_SECRET";

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
    BadRequest,
    NotFound,
    UnsupportedMediaType,
    RequestTooLarge,
    MissingSession,
    InvalidPublishTarget,
    ForwardUnavailable,
    ForwardFailed,
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

#[derive(Clone, Debug, Eq, PartialEq)]
struct BuilderGameForwardRequest {
    method: &'static str,
    path: &'static str,
    content_type: String,
    bearer_token: String,
    body: Vec<u8>,
}

struct ParsedBuilderHttpRequest {
    method: String,
    path: String,
    content_type: Option<String>,
    bearer_token: Option<String>,
    body_too_large: bool,
    body: Vec<u8>,
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
    builder_api_preflight_with_target_policy(request, builder_game, true)
}

fn builder_api_preflight_with_target_policy(
    request: &BuilderApiPreflightRequest<'_>,
    builder_game: &GameAddr,
    enforce_publish_target: bool,
) -> BuilderApiPreflightResult {
    let Some(route) = builder_api_route(request.method, request.path) else {
        return BuilderApiPreflightResult {
            code: BuilderApiPreflightCode::NotFound,
            route: None,
            http_status: 404,
            reason_code: "builder.not-found",
        };
    };

    if enforce_publish_target && builder_publish_target(builder_game).is_err() {
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

fn builder_api_handle_http_request_with_target_policy(
    request: &BuilderApiHttpRequest<'_>,
    builder_game: &GameAddr,
    enforce_publish_target: bool,
) -> BuilderApiPreflightResult {
    let preflight = BuilderApiPreflightRequest {
        method: request.method,
        path: request.path,
        content_type: request.content_type,
        body_bytes: request.body.len(),
        bearer_token: request.bearer_token,
    };
    builder_api_preflight_with_target_policy(&preflight, builder_game, enforce_publish_target)
}

fn builder_game_forward_path(operation: BuilderApiOperation) -> Option<&'static str> {
    match operation {
        BuilderApiOperation::Status => Some("/api/js-scripts/status"),
        BuilderApiOperation::Stage => Some("/api/js-scripts/stage"),
        BuilderApiOperation::Activate => Some("/api/js-scripts/activate"),
        BuilderApiOperation::Rollback => Some("/api/js-scripts/rollback"),
        BuilderApiOperation::Login
        | BuilderApiOperation::Manifest
        | BuilderApiOperation::Logout => None,
    }
}

fn validate_builder_internal_secret(secret: Option<&str>) -> Result<&str, Report> {
    let Some(secret) = secret.map(str::trim).filter(|secret| !secret.is_empty()) else {
        bail!("BuilderClient proxy trust marker is not configured");
    };
    if !is_safe_http_header_value(secret) {
        bail!("BuilderClient proxy trust marker is not valid");
    }
    Ok(secret)
}

fn builder_internal_secret_from_env() -> Result<Option<Arc<String>>, Report> {
    match std::env::var(BUILDER_INTERNAL_SECRET_ENV) {
        Ok(secret) => {
            let secret = validate_builder_internal_secret(Some(&secret))?;
            Ok(Some(Arc::new(secret.to_string())))
        }
        Err(std::env::VarError::NotPresent) => Ok(None),
        Err(std::env::VarError::NotUnicode(_)) => {
            bail!("BuilderClient proxy trust marker is not valid")
        }
    }
}

fn is_safe_http_header_value(value: &str) -> bool {
    !value.is_empty() && !value.chars().any(char::is_control)
}

fn builder_game_forward_request(
    request: &BuilderApiHttpRequest<'_>,
    route: BuilderApiRoute,
    internal_secret: Option<&str>,
) -> Result<BuilderGameForwardRequest, Report> {
    let path = builder_game_forward_path(route.operation).context("route is not forwarded")?;
    let bearer_token = request
        .bearer_token
        .map(str::trim)
        .filter(|token| !token.is_empty() && !token.contains(char::is_whitespace))
        .context("BuilderClient session token is not forwardable")?;
    validate_builder_internal_secret(internal_secret)?;
    let content_type = request
        .content_type
        .unwrap_or("application/json")
        .trim()
        .to_string();
    if !is_safe_http_header_value(&content_type) {
        bail!("BuilderClient content type is not forwardable");
    }

    Ok(BuilderGameForwardRequest {
        method: "POST",
        path,
        content_type,
        bearer_token: bearer_token.to_string(),
        body: request.body.to_vec(),
    })
}

fn builder_game_forward_request_bytes(
    forward: &BuilderGameForwardRequest,
    builder_game: &GameAddr,
    internal_secret: &str,
) -> Result<Vec<u8>, Report> {
    builder_game_forward_request_bytes_with_target_policy(
        forward,
        builder_game,
        internal_secret,
        true,
    )
}

fn builder_game_forward_request_bytes_with_target_policy(
    forward: &BuilderGameForwardRequest,
    builder_game: &GameAddr,
    internal_secret: &str,
    enforce_publish_target: bool,
) -> Result<Vec<u8>, Report> {
    if enforce_publish_target {
        builder_publish_target(builder_game)?;
    }
    let internal_secret = validate_builder_internal_secret(Some(internal_secret))?;
    let host = format!("{}:{}", builder_game.hostname, builder_game.port);
    let request_head = format!(
        "{} {} HTTP/1.1\r\nhost: {}\r\ncontent-type: {}\r\ncontent-length: {}\r\nauthorization: Bearer {}\r\n{}: {}\r\nconnection: close\r\n\r\n",
        forward.method,
        forward.path,
        host,
        forward.content_type,
        forward.body.len(),
        forward.bearer_token,
        BUILDER_INTERNAL_TRUST_HEADER,
        internal_secret
    );
    let mut bytes = request_head.into_bytes();
    bytes.extend_from_slice(&forward.body);
    Ok(bytes)
}

async fn forward_builder_api_to_game(
    request: &BuilderApiHttpRequest<'_>,
    route: BuilderApiRoute,
    builder_game: &GameAddr,
    internal_secret: Option<&str>,
    enforce_publish_target: bool,
) -> Result<Vec<u8>, Report> {
    timeout(
        BUILDER_GAME_FORWARD_TIMEOUT,
        forward_builder_api_to_game_inner(
            request,
            route,
            builder_game,
            internal_secret,
            enforce_publish_target,
        ),
    )
    .await?
}

async fn forward_builder_api_to_game_inner(
    request: &BuilderApiHttpRequest<'_>,
    route: BuilderApiRoute,
    builder_game: &GameAddr,
    internal_secret: Option<&str>,
    enforce_publish_target: bool,
) -> Result<Vec<u8>, Report> {
    let internal_secret = validate_builder_internal_secret(internal_secret)?;
    let forward = builder_game_forward_request(request, route, Some(internal_secret))?;
    let bytes = builder_game_forward_request_bytes_with_target_policy(
        &forward,
        builder_game,
        internal_secret,
        enforce_publish_target,
    )?;
    let mut game = timeout(BUILDER_GAME_FORWARD_TIMEOUT, builder_game.connect()).await??;
    timeout(BUILDER_GAME_FORWARD_TIMEOUT, game.write_all(&bytes)).await??;
    timeout(BUILDER_GAME_FORWARD_TIMEOUT, game.shutdown()).await??;

    let mut response = Vec::new();
    let mut buffer = [0u8; 1024];
    loop {
        let read = timeout(BUILDER_GAME_FORWARD_TIMEOUT, game.read(&mut buffer)).await??;
        if read == 0 {
            break;
        }
        if response.len() + read > MAX_BUILDER_API_RESPONSE_BYTES {
            bail!("BuilderClient game response exceeds limit");
        }
        response.extend_from_slice(&buffer[..read]);
    }
    if response.is_empty() {
        bail!("BuilderClient game response is empty");
    }
    sanitized_builder_game_response(&response, internal_secret)
}

fn sanitized_builder_game_response(
    response: &[u8],
    internal_secret: &str,
) -> Result<Vec<u8>, Report> {
    if response
        .windows(internal_secret.len())
        .any(|window| window == internal_secret.as_bytes())
    {
        bail!("BuilderClient game response exposed internal trust marker");
    }
    let Some(header_end) = response.windows(4).position(|window| window == b"\r\n\r\n") else {
        bail!("BuilderClient game response is not complete HTTP");
    };
    let header_bytes = &response[..header_end];
    let body = &response[header_end + 4..];
    let header_text = std::str::from_utf8(header_bytes)?;
    let mut lines = header_text.split("\r\n");
    let status_line = lines.next().context("missing backend status line")?;
    let mut status_parts = status_line.split_whitespace();
    let version = status_parts
        .next()
        .context("missing backend http version")?;
    if version != "HTTP/1.0" && version != "HTTP/1.1" {
        bail!("unsupported backend http version");
    }
    let status = status_parts
        .next()
        .context("missing backend http status")?
        .parse::<u16>()?;
    if !(100..=599).contains(&status) {
        bail!("invalid backend http status");
    }

    let mut content_type = "application/json".to_string();
    for line in lines {
        let Some((name, value)) = line.split_once(':') else {
            bail!("invalid backend response header");
        };
        if name.eq_ignore_ascii_case("content-type") {
            let value = value.trim();
            if !is_safe_http_header_value(value) {
                bail!("invalid backend content type");
            }
            content_type = value.to_string();
        }
    }

    let head = format!(
        "HTTP/1.1 {} {}\r\ncontent-type: {}\r\ncontent-length: {}\r\nconnection: close\r\n\r\n",
        status,
        http_status_text(status),
        content_type,
        body.len()
    );
    let mut sanitized = head.into_bytes();
    sanitized.extend_from_slice(body);
    Ok(sanitized)
}

fn builder_api_response_body(result: &BuilderApiPreflightResult) -> String {
    format!(
        "{{\"ok\":{},\"reasonCode\":\"{}\"}}",
        result.code == BuilderApiPreflightCode::Accepted,
        result.reason_code
    )
}

fn http_status_text(status: u16) -> &'static str {
    match status {
        200 => "OK",
        202 => "Accepted",
        400 => "Bad Request",
        401 => "Unauthorized",
        404 => "Not Found",
        413 => "Payload Too Large",
        415 => "Unsupported Media Type",
        502 => "Bad Gateway",
        503 => "Service Unavailable",
        _ => "Internal Server Error",
    }
}

fn builder_api_error_result(
    code: BuilderApiPreflightCode,
    http_status: u16,
    reason_code: &'static str,
) -> BuilderApiPreflightResult {
    BuilderApiPreflightResult {
        code,
        route: None,
        http_status,
        reason_code,
    }
}

async fn write_builder_api_response(
    stream: &mut TcpStream,
    result: &BuilderApiPreflightResult,
) -> Result<(), Report> {
    let body = builder_api_response_body(result);
    let response = format!(
        "HTTP/1.1 {} {}\r\ncontent-type: application/json\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{}",
        result.http_status,
        http_status_text(result.http_status),
        body.len(),
        body
    );
    stream.write_all(response.as_bytes()).await?;
    Ok(())
}

fn parse_builder_api_headers(
    header_bytes: &[u8],
) -> Result<(String, String, HashMap<String, Vec<String>>), Report> {
    let header_text = std::str::from_utf8(header_bytes)?;
    let mut lines = header_text.split("\r\n");
    let request_line = lines.next().context("missing request line")?;
    let mut request_parts = request_line.split_whitespace();
    let method = request_parts.next().context("missing method")?.to_string();
    let path = request_parts.next().context("missing path")?.to_string();
    let version = request_parts.next().context("missing http version")?;
    if !version.starts_with("HTTP/") {
        bail!("invalid http version");
    }

    let mut headers: HashMap<String, Vec<String>> = HashMap::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let Some((name, value)) = line.split_once(':') else {
            bail!("invalid http header");
        };
        headers
            .entry(name.trim().to_ascii_lowercase())
            .or_default()
            .push(value.trim().to_string());
    }

    Ok((method, path, headers))
}

fn single_header<'a>(
    headers: &'a HashMap<String, Vec<String>>,
    name: &str,
) -> Result<Option<&'a str>, Report> {
    let Some(values) = headers.get(name) else {
        return Ok(None);
    };
    if values.len() != 1 {
        bail!("duplicate http header");
    }
    Ok(values.first().map(String::as_str))
}

fn bearer_token_from_authorization(value: Option<&str>) -> Option<String> {
    let value = value?.trim();
    let (scheme, token) = value.split_once(' ')?;
    if !scheme.eq_ignore_ascii_case("bearer") {
        return None;
    }
    let token = token.trim();
    if token.is_empty() || token.contains(char::is_whitespace) {
        return None;
    }
    Some(token.to_string())
}

async fn read_builder_http_request(
    stream: &mut TcpStream,
) -> Result<ParsedBuilderHttpRequest, Report> {
    let mut bytes = Vec::new();
    let mut buffer = [0u8; 1024];
    let header_end;
    loop {
        let read = timeout(BUILDER_API_READ_TIMEOUT, stream.read(&mut buffer)).await??;
        if read == 0 {
            bail!("connection closed before headers");
        }
        if bytes.len() + read > MAX_BUILDER_API_HEADER_BYTES {
            bail!("builder api headers exceed limit");
        }
        bytes.extend_from_slice(&buffer[..read]);
        if let Some(position) = bytes.windows(4).position(|window| window == b"\r\n\r\n") {
            header_end = position + 4;
            break;
        }
    }

    let (method, path, headers) = parse_builder_api_headers(&bytes[..header_end])?;
    if headers.contains_key("transfer-encoding") {
        bail!("transfer encoding is not supported");
    }
    let route = builder_api_route(&method, &path);
    let content_length_header = single_header(&headers, "content-length")?;
    if route
        .map(|route| route.operation.requires_json_body())
        .unwrap_or(false)
        && content_length_header.is_none()
    {
        bail!("json request body length is required");
    }
    let content_length = content_length_header
        .map(|value| value.parse::<usize>())
        .transpose()?
        .unwrap_or(0);
    let body_too_large = content_length > MAX_BUILDER_API_REQUEST_BYTES;
    let mut body = Vec::new();
    if !body_too_large {
        body.extend_from_slice(&bytes[header_end..]);
        while body.len() < content_length {
            let read = timeout(BUILDER_API_READ_TIMEOUT, stream.read(&mut buffer)).await??;
            if read == 0 {
                bail!("connection closed before body");
            }
            body.extend_from_slice(&buffer[..read]);
            if body.len() > MAX_BUILDER_API_REQUEST_BYTES {
                bail!("builder api body exceeds limit");
            }
        }
        body.truncate(content_length);
    }

    Ok(ParsedBuilderHttpRequest {
        method,
        path,
        content_type: single_header(&headers, "content-type")?.map(str::to_string),
        bearer_token: bearer_token_from_authorization(single_header(&headers, "authorization")?),
        body_too_large,
        body,
    })
}

async fn handle_builder_api(
    stream: TcpStream,
    builder_game: GameAddr,
    builder_internal_secret: Option<Arc<String>>,
) -> Result<(), Report> {
    handle_builder_api_with_target_policy(stream, builder_game, builder_internal_secret, true).await
}

async fn handle_builder_api_with_target_policy(
    mut stream: TcpStream,
    builder_game: GameAddr,
    builder_internal_secret: Option<Arc<String>>,
    enforce_publish_target: bool,
) -> Result<(), Report> {
    let request = match read_builder_http_request(&mut stream).await {
        Ok(request) => request,
        Err(err) => {
            log::debug!("Rejected malformed BuilderClient API request: {err}");
            let result = builder_api_error_result(
                BuilderApiPreflightCode::BadRequest,
                400,
                "builder.bad-request",
            );
            write_builder_api_response(&mut stream, &result).await?;
            return Ok(());
        }
    };
    let body_storage;
    let body = if request.body_too_large {
        body_storage = vec![0u8; MAX_BUILDER_API_REQUEST_BYTES + 1];
        &body_storage
    } else {
        &request.body
    };
    let http_request = BuilderApiHttpRequest {
        method: &request.method,
        path: &request.path,
        content_type: request.content_type.as_deref(),
        bearer_token: request.bearer_token.as_deref(),
        body,
    };
    let result = builder_api_handle_http_request_with_target_policy(
        &http_request,
        &builder_game,
        enforce_publish_target,
    );
    if result.code != BuilderApiPreflightCode::Accepted {
        return write_builder_api_response(&mut stream, &result).await;
    }

    let route = result
        .route
        .context("accepted BuilderClient route is missing")?;
    if builder_game_forward_path(route.operation).is_none() {
        return write_builder_api_response(&mut stream, &result).await;
    }

    if builder_internal_secret
        .as_deref()
        .map(|secret| validate_builder_internal_secret(Some(secret.as_str())).is_ok())
        != Some(true)
    {
        log::debug!("BuilderClient publish forwarding rejected: missing internal trust marker");
        let result = builder_api_error_result(
            BuilderApiPreflightCode::ForwardUnavailable,
            503,
            "builder.forward-unavailable",
        );
        return write_builder_api_response(&mut stream, &result).await;
    }

    match forward_builder_api_to_game(
        &http_request,
        route,
        &builder_game,
        builder_internal_secret
            .as_deref()
            .map(|secret| secret.as_str()),
        enforce_publish_target,
    )
    .await
    {
        Ok(response) => {
            stream.write_all(&response).await?;
            Ok(())
        }
        Err(err) => {
            log::debug!("BuilderClient publish forwarding failed: {err}");
            let result = builder_api_error_result(
                BuilderApiPreflightCode::ForwardFailed,
                502,
                "builder.forward-failed",
            );
            write_builder_api_response(&mut stream, &result).await
        }
    }
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

    #[arg(long, default_value = "127.0.0.1:8081")]
    /// HTTP address to listen for BuilderClient API requests
    builder_api: SocketAddrV4,

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

async fn builder_api_server(
    builder_game: GameAddr,
    builder_internal_secret: Option<Arc<String>>,
    listener: TcpListener,
) -> Result<(), Report> {
    loop {
        let (stream, addr) = listener.accept().await?;
        let builder_game = builder_game.clone();
        let builder_internal_secret = builder_internal_secret.clone();

        log::debug!("Received BuilderClient API connection on {addr}");

        tokio::spawn(async move {
            if let Err(err) =
                handle_builder_api(stream, builder_game, builder_internal_secret).await
            {
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
    let builder_api = TcpListener::bind(args.builder_api).await?;

    if let Ok(addr) = tcp.local_addr() {
        log::info!("Listening for TCP connections on {}", addr);
    }

    if let Ok(addr) = ws.local_addr() {
        log::info!("Listening for WebSocket connections on {}", addr);
    }

    if let Ok(addr) = builder_api.local_addr() {
        log::info!("Listening for BuilderClient API connections on {}", addr);
    }

    let tcp = tokio::spawn(tcp_server(args.game.clone(), tcp));
    let ws = tokio::spawn(ws_server(args.game, ws, args.cloudflare));
    let builder_internal_secret = builder_internal_secret_from_env()?;
    let builder_api = tokio::spawn(builder_api_server(
        args.builder_game,
        builder_internal_secret,
        builder_api,
    ));

    tokio::select! {
        res = tcp => res??,
        res = ws => res??,
        res = builder_api => res??,
    };

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;
    use tokio::net::TcpListener;

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
    fn builder_game_forward_paths_translate_publish_routes_only() {
        assert_eq!(
            Some("/api/js-scripts/status"),
            builder_game_forward_path(BuilderApiOperation::Status)
        );
        assert_eq!(
            Some("/api/js-scripts/stage"),
            builder_game_forward_path(BuilderApiOperation::Stage)
        );
        assert_eq!(
            Some("/api/js-scripts/activate"),
            builder_game_forward_path(BuilderApiOperation::Activate)
        );
        assert_eq!(
            Some("/api/js-scripts/rollback"),
            builder_game_forward_path(BuilderApiOperation::Rollback)
        );
        assert_eq!(None, builder_game_forward_path(BuilderApiOperation::Login));
        assert_eq!(
            None,
            builder_game_forward_path(BuilderApiOperation::Manifest)
        );
        assert_eq!(None, builder_game_forward_path(BuilderApiOperation::Logout));
    }

    #[test]
    fn builder_game_forward_request_adds_trust_marker_and_preserves_body() {
        let route = builder_api_route("POST", "/api/builder/js/stage").unwrap();
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json;charset=utf-8"),
            bearer_token: Some("token:builder"),
            body: br#"{"package":{"packageId":"js:30:character:3001"}}"#,
        };
        let forward = builder_game_forward_request(&request, route, Some("local-secret")).unwrap();
        let bytes = builder_game_forward_request_bytes(
            &forward,
            &game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            "local-secret",
        )
        .unwrap();
        let rendered = String::from_utf8(bytes).unwrap();

        assert!(rendered.starts_with("POST /api/js-scripts/stage HTTP/1.1\r\n"));
        assert!(rendered.contains("host: 127.0.0.1:4802\r\n"));
        assert!(rendered.contains("content-type: application/json;charset=utf-8\r\n"));
        assert!(rendered.contains("authorization: Bearer token:builder\r\n"));
        assert!(rendered.contains("x-rots-builder-proxy-secret: local-secret\r\n"));
        assert!(rendered.ends_with(r#"{"package":{"packageId":"js:30:character:3001"}}"#));
    }

    #[test]
    fn builder_game_forward_request_rejects_missing_or_malformed_trust_marker() {
        let route = builder_api_route("POST", "/api/builder/js/stage").unwrap();
        let request = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json"),
            bearer_token: Some("token:builder"),
            body: b"{}",
        };

        assert!(builder_game_forward_request(&request, route, None).is_err());
        assert!(builder_game_forward_request(&request, route, Some(" ")).is_err());
        assert!(builder_game_forward_request(&request, route, Some("secret\r\nbad")).is_err());
    }

    #[test]
    fn builder_game_forward_request_rejects_malformed_forwarded_headers() {
        let route = builder_api_route("POST", "/api/builder/js/stage").unwrap();
        let bad_content_type = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json\r\nx-extra: bad"),
            bearer_token: Some("token:builder"),
            body: b"{}",
        };
        let bad_token = BuilderApiHttpRequest {
            method: "POST",
            path: "/api/builder/js/stage",
            content_type: Some("application/json"),
            bearer_token: Some("token:builder extra"),
            body: b"{}",
        };

        assert!(builder_game_forward_request(&bad_content_type, route, Some("secret")).is_err());
        assert!(builder_game_forward_request(&bad_token, route, Some("secret")).is_err());
    }

    #[test]
    fn builder_game_forward_request_rejects_live_target_generically() {
        let forward = BuilderGameForwardRequest {
            method: "POST",
            path: "/api/js-scripts/stage",
            content_type: String::from("application/json"),
            bearer_token: String::from("token:builder"),
            body: b"{}".to_vec(),
        };
        let error = builder_game_forward_request_bytes(
            &forward,
            &game_addr("127.0.0.1", LIVE_GAME_PORT),
            "secret",
        )
        .expect_err("live target must be rejected")
        .to_string();

        assert!(error.contains("invalid BuilderClient publish target"));
        assert!(!error.contains("3791"));
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

    async fn roundtrip_builder_api_request(
        raw_request: &[u8],
        builder_game: GameAddr,
        builder_internal_secret: Option<&str>,
    ) -> String {
        roundtrip_builder_api_request_with_target_policy(
            raw_request,
            builder_game,
            builder_internal_secret,
            true,
        )
        .await
    }

    async fn roundtrip_builder_api_request_with_target_policy(
        raw_request: &[u8],
        builder_game: GameAddr,
        builder_internal_secret: Option<&str>,
        enforce_publish_target: bool,
    ) -> String {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        let builder_internal_secret =
            builder_internal_secret.map(|secret| Arc::new(String::from(secret)));
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_builder_api_with_target_policy(
                stream,
                builder_game,
                builder_internal_secret,
                enforce_publish_target,
            )
            .await
            .unwrap();
        });

        let mut client = TcpStream::connect(addr).await.unwrap();
        client.write_all(raw_request).await.unwrap();
        client.shutdown().await.unwrap();
        let mut response = Vec::new();
        client.read_to_end(&mut response).await.unwrap();
        server.await.unwrap();
        String::from_utf8(response).unwrap()
    }

    #[tokio::test]
    async fn builder_api_handler_forwards_publish_request_and_sanitizes_backend_response() {
        let game_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let local_game_addr = game_listener.local_addr().unwrap();
        let game = tokio::spawn(async move {
            let (mut stream, _) = game_listener.accept().await.unwrap();
            let mut forwarded = Vec::new();
            stream.read_to_end(&mut forwarded).await.unwrap();
            stream
                .write_all(
                    b"HTTP/1.1 202 Accepted\r\ncontent-type: application/json\r\nx-rots-builder-proxy-secret: stripped\r\nconnection: keep-alive\r\ncontent-length: 11\r\n\r\n{\"ok\":true}",
                )
                .await
                .unwrap();
            String::from_utf8(forwarded).unwrap()
        });

        let response = roundtrip_builder_api_request_with_target_policy(
            b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\ncontent-length: 2\r\n\r\n{}",
            game_addr("127.0.0.1", local_game_addr.port()),
            Some("local-secret"),
            false,
        )
        .await;
        let forwarded = game.await.unwrap();

        assert!(forwarded.starts_with("POST /api/js-scripts/stage HTTP/1.1\r\n"));
        assert!(forwarded.contains("authorization: Bearer token:builder\r\n"));
        assert!(forwarded.contains("x-rots-builder-proxy-secret: local-secret\r\n"));
        assert!(forwarded.ends_with("{}"));
        assert_eq!("HTTP/1.1 202 Accepted", response_status(&response));
        assert!(response.contains("content-type: application/json\r\n"));
        assert!(response.contains("content-length: 11\r\n"));
        assert!(response.ends_with("{\"ok\":true}"));
        assert!(!response.contains("x-rots-builder-proxy-secret"));
        assert!(!response.contains("local-secret"));
        assert!(!response.contains("keep-alive"));
    }

    #[tokio::test]
    async fn builder_api_handler_redacts_malformed_backend_response() {
        let game_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let local_game_addr = game_listener.local_addr().unwrap();
        let game = tokio::spawn(async move {
            let (mut stream, _) = game_listener.accept().await.unwrap();
            let mut forwarded = Vec::new();
            stream.read_to_end(&mut forwarded).await.unwrap();
            stream.write_all(b"not http").await.unwrap();
        });

        let response = roundtrip_builder_api_request_with_target_policy(
            b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\ncontent-length: 2\r\n\r\n{}",
            game_addr("127.0.0.1", local_game_addr.port()),
            Some("local-secret"),
            false,
        )
        .await;
        game.await.unwrap();

        assert_eq!("HTTP/1.1 502 Bad Gateway", response_status(&response));
        assert!(response.contains("\"reasonCode\":\"builder.forward-failed\""));
        assert!(!response.contains("local-secret"));
        assert!(!response.contains("token:builder"));
    }

    fn response_status(response: &str) -> &str {
        response.lines().next().unwrap_or_default()
    }

    #[tokio::test]
    async fn builder_api_handler_requires_trust_marker_before_forwarding_stage_request() {
        let response = roundtrip_builder_api_request(
            b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\ncontent-length: 2\r\n\r\n{}",
            game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            None,
        )
        .await;

        assert!(response.starts_with("HTTP/1.1 503 Service Unavailable"));
        assert!(response.contains("\"reasonCode\":\"builder.forward-unavailable\""));
        assert!(response.contains("connection: close"));
        assert!(!response.contains("token:builder"));
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_missing_session() {
        let response = roundtrip_builder_api_request(
            b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\ncontent-length: 2\r\n\r\n{}",
            game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            None,
        )
        .await;

        assert!(response.starts_with("HTTP/1.1 401 Unauthorized"));
        assert!(response.contains("\"reasonCode\":\"builder.missing-session\""));
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_oversized_body_before_reading_body() {
        let request = format!(
            "POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\ncontent-length: {}\r\n\r\n",
            MAX_BUILDER_API_REQUEST_BYTES + 1
        );
        let response = roundtrip_builder_api_request(
            request.as_bytes(),
            game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            None,
        )
        .await;

        assert!(response.starts_with("HTTP/1.1 413 Payload Too Large"));
        assert!(response.contains("\"reasonCode\":\"builder.request-too-large\""));
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_invalid_target_generically() {
        let response = roundtrip_builder_api_request(
            b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\ncontent-length: 2\r\n\r\n{}",
            game_addr("127.0.0.1", LIVE_GAME_PORT),
            None,
        )
        .await;

        assert!(response.starts_with("HTTP/1.1 503 Service Unavailable"));
        assert!(response.contains("\"reasonCode\":\"builder.invalid-target\""));
        assert!(!response.contains("3791"));
        assert!(!response.contains("4802"));
    }

    #[tokio::test]
    async fn builder_api_handler_returns_redacted_bad_request_for_malformed_http() {
        let response = roundtrip_builder_api_request(
            b"POST /api/builder/js/stage\r\nbad-header\r\n\r\n",
            game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            None,
        )
        .await;

        assert!(response.starts_with("HTTP/1.1 400 Bad Request"));
        assert!(response.contains("\"reasonCode\":\"builder.bad-request\""));
        assert!(!response.contains("bad-header"));
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_duplicate_content_length() {
        let response = roundtrip_builder_api_request(
            b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\ncontent-length: 2\r\ncontent-length: 2\r\n\r\n{}",
            game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            None,
        )
        .await;

        assert_eq!("HTTP/1.1 400 Bad Request", response_status(&response));
        assert!(response.contains("\"reasonCode\":\"builder.bad-request\""));
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_transfer_encoding() {
        let response = roundtrip_builder_api_request(
            b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\ntransfer-encoding: chunked\r\ncontent-length: 2\r\n\r\n{}",
            game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            None,
        )
        .await;

        assert_eq!("HTTP/1.1 400 Bad Request", response_status(&response));
        assert!(response.contains("\"reasonCode\":\"builder.bad-request\""));
    }

    #[tokio::test]
    async fn builder_api_handler_requires_content_length_for_json_routes() {
        let response = roundtrip_builder_api_request(
            b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\n\r\n{}",
            game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            None,
        )
        .await;

        assert_eq!("HTTP/1.1 400 Bad Request", response_status(&response));
        assert!(response.contains("\"reasonCode\":\"builder.bad-request\""));
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_header_terminator_after_limit() {
        let filler = "x".repeat(MAX_BUILDER_API_HEADER_BYTES + 1);
        let request = format!("POST /api/builder/js/stage HTTP/1.1\r\nx-fill: {filler}\r\n\r\n");
        let response = roundtrip_builder_api_request(
            request.as_bytes(),
            game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
            None,
        )
        .await;

        assert_eq!("HTTP/1.1 400 Bad Request", response_status(&response));
        assert!(response.contains("\"reasonCode\":\"builder.bad-request\""));
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_malformed_authorization_values() {
        for authorization in [
            "Bearer",
            "Bearer ",
            "Bearer token extra",
            "Basic token",
            "Bearer\ttoken",
        ] {
            let request = format!(
                "POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: {authorization}\r\ncontent-length: 2\r\n\r\n{{}}"
            );
            let response = roundtrip_builder_api_request(
                request.as_bytes(),
                game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT),
                None,
            )
            .await;

            assert_eq!("HTTP/1.1 401 Unauthorized", response_status(&response));
            assert!(response.contains("\"reasonCode\":\"builder.missing-session\""));
        }
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_partial_header_timeout() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_builder_api(stream, game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT), None)
                .await
                .unwrap();
        });

        let mut client = TcpStream::connect(addr).await.unwrap();
        client
            .write_all(b"POST /api/builder/js/stage HTTP/1.1\r\n")
            .await
            .unwrap();
        let mut response = Vec::new();
        client.read_to_end(&mut response).await.unwrap();
        server.await.unwrap();
        let response = String::from_utf8(response).unwrap();

        assert_eq!("HTTP/1.1 400 Bad Request", response_status(&response));
        assert!(response.contains("\"reasonCode\":\"builder.bad-request\""));
    }

    #[tokio::test]
    async fn builder_api_handler_rejects_partial_body_timeout() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_builder_api(stream, game_addr("127.0.0.1", BUILDER_TEST_GAME_PORT), None)
                .await
                .unwrap();
        });

        let mut client = TcpStream::connect(addr).await.unwrap();
        client
            .write_all(b"POST /api/builder/js/stage HTTP/1.1\r\nhost: localhost\r\ncontent-type: application/json\r\nauthorization: Bearer token:builder\r\ncontent-length: 8\r\n\r\n{}")
            .await
            .unwrap();
        let mut response = Vec::new();
        client.read_to_end(&mut response).await.unwrap();
        server.await.unwrap();
        let response = String::from_utf8(response).unwrap();

        assert_eq!("HTTP/1.1 400 Bad Request", response_status(&response));
        assert!(response.contains("\"reasonCode\":\"builder.bad-request\""));
    }
}
