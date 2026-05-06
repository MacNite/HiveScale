# HiveScale — Docker Installation Guide

This guide covers deploying the HiveScale backend on any Linux system with Docker and Docker Compose. The process is essentially the same as the [TrueNAS installation](truenas-install.md), but without the TrueNAS-specific UI steps.

---

## Prerequisites

- A Linux server or VPS (Ubuntu 22.04+ / Debian 12+ recommended)
- [Docker Engine](https://docs.docker.com/engine/install/) ≥ 24
- [Docker Compose](https://docs.docker.com/compose/install/) ≥ 2 (usually bundled as `docker compose`)
- A directory for persistent database storage (e.g. `/opt/hivescale/db`)

---

## Step 1 — Create storage directories

```bash
sudo mkdir -p /opt/hivescale/db
sudo mkdir -p /opt/hivescale/firmware   # optional: for OTA binary hosting
```

---

## Step 2 — Create the environment file

```bash
cd /opt/hivescale
cp /path/to/repo/docker/.env.example .env
nano .env
```

Fill in all values:

```env
# API key used by the ESP32 firmware (X-API-Key header)
API_KEY=change-this-to-a-long-random-string

# Service key used by HivePal (X-HivePal-Service-Key header)
HIVEPAL_SERVICE_API_KEY=change-this-to-another-long-random-string

# PostgreSQL password — must match POSTGRES_PASSWORD below
POSTGRES_PASSWORD=change-this-database-password

# Public base URL used to build OTA firmware download links
PUBLIC_BASE_URL=http://your-server-ip-or-domain:31115

# Timezone for logs and timestamps
TZ=Europe/Berlin
```

> Generate strong random keys with:
> ```bash
> openssl rand -hex 32
> ```

---

## Step 3 — Create the Docker Compose file

Copy the compose file from the repo or create `/opt/hivescale/docker-compose.yml`:

```yaml
services:
  hivescale-api:
    image: ghcr.io/macnite/hivescale-api:latest
    depends_on:
      hivescale-db:
        condition: service_healthy
    environment:
      API_KEY: ${API_KEY}
      HIVEPAL_SERVICE_API_KEY: ${HIVEPAL_SERVICE_API_KEY}
      DATABASE_URL: postgresql://hivescale:${POSTGRES_PASSWORD}@hivescale-db:5432/hivescale
      PUBLIC_BASE_URL: ${PUBLIC_BASE_URL}
      FIRMWARE_DIR: /app/firmware
      TZ: ${TZ}
    ports:
      - "31115:8000"
    volumes:
      - /opt/hivescale/firmware:/app/firmware
    restart: unless-stopped

  hivescale-db:
    image: postgres:16-alpine
    environment:
      POSTGRES_DB: hivescale
      POSTGRES_USER: hivescale
      POSTGRES_PASSWORD: ${POSTGRES_PASSWORD}
      TZ: ${TZ}
    healthcheck:
      test:
        - CMD-SHELL
        - pg_isready -U hivescale -d hivescale
      interval: 30s
      timeout: 5s
      retries: 5
    restart: unless-stopped
    volumes:
      - /opt/hivescale/db:/var/lib/postgresql/data
```

---

## Step 4 — Start the stack

```bash
cd /opt/hivescale
docker compose up -d
```

Check that both containers are running:

```bash
docker compose ps
```

View live logs:

```bash
docker compose logs -f
```

---

## Step 5 — Verify

```bash
curl http://localhost:31115/health
```

Expected:
```json
{ "status": "ok" }
```

Interactive API docs: `http://your-server-ip:31115/docs`

---

## Step 6 — Configure the firmware

Edit `firmware/include/secrets.h`:

```cpp
#define API_BASE_URL  "http://your-server-ip-or-domain:31115"
#define API_KEY       "change-this-to-a-long-random-string"
```

Re-flash the device, or update via the provisioning portal if it is already deployed.

---

## OTA firmware updates

Place compiled firmware binaries in `/opt/hivescale/firmware/`, then register them:

```bash
curl -X POST http://localhost:31115/api/v1/firmware/releases \
  -H "X-API-Key: YOUR_API_KEY" \
  -H "Content-Type: application/json" \
  -d '{"version": "0.5.0", "filename": "hivescale-0.5.0.bin", "active": true}'
```

The device checks for updates every 6 hours and on every measurement cycle.

---

## Updating the application

```bash
cd /opt/hivescale
docker compose pull
docker compose up -d
```

The database volume is preserved — no data is lost on updates.

---

## Backups

**Logical dump (recommended):**
```bash
docker compose exec hivescale-db pg_dump -U hivescale hivescale > hivescale-backup-$(date +%Y%m%d).sql
```

**Restore from dump:**
```bash
docker compose exec -T hivescale-db psql -U hivescale hivescale < hivescale-backup-20260501.sql
```

---

## Exposing to the internet (optional)

If you want the ESP32 to reach the server from outside your LAN (e.g. when the beehives are in a field with mobile data), you have several options:

- **Reverse proxy with HTTPS** — run Nginx or Caddy in front of the API and obtain a TLS certificate via Let's Encrypt. Caddy does this automatically with a single config line.
- **Tailscale / WireGuard** — put both the server and the ESP32 (via a companion device or router) on a private VPN.
- **Port forwarding** — forward port `31115` on your router to the server. This works but exposes the API directly; ensure your API keys are strong.

---

## Troubleshooting

**Containers exit on startup:**
```bash
docker compose logs hivescale-api
docker compose logs hivescale-db
```
Common causes: wrong `DATABASE_URL` password, missing environment variable, or port `31115` already in use.

**Port already in use:**
```bash
sudo ss -tlnp | grep 31115
```
Change the host-side port mapping in the compose file (e.g. `"31116:8000"`) if something else is using `31115`.

**Measurements not stored:**
Use the test commands in [test-commands.md](test-commands.md) to verify the API key and payload format.

**OTA firmware URL does not resolve:**
Ensure `PUBLIC_BASE_URL` is set to an address the ESP32 can reach — not `localhost`.