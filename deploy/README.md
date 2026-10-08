# Hosting the game

The web version has two parts:
- **The page** (`index.html`, `.js`, `.wasm` and `.data`) is built by GitHub Actions and published on GitHub Pages.
- **The server** (`minicraft-server`) runs in Docker on a small Linux machine. Caddy sits in front of it and adds
  HTTPS: a page served over HTTPS may only open `wss://` connections, so the server needs a domain name and a
  certificate.

```
browser ── https ──▶ GitHub Pages (the game)
browser ── wss ────▶ Caddy :443 ── ws ──▶ minicraft-server :7777
```

The steps below use free services:
- an Oracle Cloud "Always Free" virtual machine;
- a DuckDNS subdomain.

Any Linux machine with Docker and ports 80/443 open works the same way.

## 1. A virtual machine (Oracle Cloud Always Free)

1. Create an account at <https://www.oracle.com/cloud/free/>. It asks for a card for identity checks; Always
   Free resources are not charged.
2. Pick the **home region** closest to your players. Every key press makes a round trip to the server, so
   distance shows up as input lag.
3. Create a compute instance:
   - **Image:** Ubuntu 24.04.
   - **Shape:** `VM.Standard.A1.Flex` (Ampere, Always Free eligible), 1 OCPU and 6 GB is plenty. If the region
     has no Ampere capacity, use `VM.Standard.E2.1.Micro`.
   - Add your SSH public key. Keep the public IP it shows.
4. **Open ports 80 and 443:**
   - In the instance's subnet: *Networking → Virtual Cloud Networks → your VCN → Security Lists → Default →
     Add Ingress Rules*. Add source `0.0.0.0/0`, TCP, destination ports `80` and `443`.
   - Oracle's Ubuntu images also block them in the machine's own firewall. Over SSH:
     ```sh
     sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 80 -j ACCEPT
     sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 443 -j ACCEPT
     sudo netfilter-persistent save
     ```
5. **Keep it from being reclaimed:** Oracle may reclaim Always Free instances that look idle, and a quiet game
   server does. Upgrading the account to *Pay As You Go* (Billing → Upgrade) stops that. Always Free resources
   stay free after the upgrade.

## 2. A domain name (DuckDNS)

1. Sign in at <https://www.duckdns.org>.
2. Create a subdomain, e.g. `minicraft-yourname`.
3. Set its IP to the VM's public IP.

The server's address is then `minicraft-yourname.duckdns.org`.

## 3. Start the server

On the VM:
```sh
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER && newgrp docker

git clone https://github.com/AdnaneBJA/Minicraft.git
cd Minicraft/deploy
cat > .env <<EOF
DOMAIN=minicraft-yourname.duckdns.org
POSTGRES_PASSWORD=$(openssl rand -hex 24)
STATS_TOKEN=$(openssl rand -hex 24)
EOF
docker compose up -d --build
```

`.env` holds the domain and two random secrets: the stats database's password, and the token the game server sends
to the stats service. Keep the file private; it never goes into git.

What this does:
- **The first build** takes a few minutes: it compiles the server and the stats service and runs their tests.
- **Caddy** then gets a Let's Encrypt certificate on its own; this needs ports 80 and 443 open.
- **Restarts:** every container restarts by itself after a crash or a reboot. The stats live in the `pgdata` volume,
  so they survive restarts and updates.
- **The dashboard** is at `https://minicraft-yourname.duckdns.org/stats`.

Check it:
```sh
docker compose logs -f server     # "minicraft-server listening on port 7777", then players connecting
curl -i --http1.1 https://minicraft-yourname.duckdns.org -H "Connection: Upgrade" -H "Upgrade: websocket" \
     -H "Sec-WebSocket-Version: 13" -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ=="
# expect: HTTP/1.1 101 Switching Protocols
```

## 4. Point the web version at it

In the GitHub repository:
1. **Settings → Secrets and variables → Actions → Variables → New repository variable:** set
   `MINICRAFT_SERVER_URL` to `wss://minicraft-yourname.duckdns.org`.
2. **Settings → Pages → Build and deployment → Source:** choose *GitHub Actions*.
3. **Actions → Web → Run workflow** (or push to `main`).

The game is then at `https://<github-user>.github.io/Minicraft/`.

## Everyday operations

| What | How (in `Minicraft/deploy` on the VM) |
|---|---|
| Update to the latest code | `git pull && docker compose up -d --build` |
| Server log | `docker compose logs -f server` |
| Stats service log | `docker compose logs -f stats` |
| Back up the stats | `docker compose exec postgres pg_dump -U stats stats > stats-backup.sql` |
| Restart | `docker compose restart server` |
| Stop everything | `docker compose down` |

**Updating from a version without stats:** add the two new lines to `.env` (`POSTGRES_PASSWORD` and `STATS_TOKEN`,
each `openssl rand -hex 24`), then `git pull && docker compose up -d --build`.

Restarting the server closes every world. Worlds live only in the server's memory, so they last as long as
someone is playing in them.

## Running it locally

- **Without Caddy:**
  - run `docker build -f server/Dockerfile -t minicraft-server .` then
    `docker run --rm -p 7777:7777 minicraft-server`, from the repository root;
  - build the web version with `-DMINICRAFT_SERVER_URL=ws://localhost:7777`.
- **With Caddy:** `echo DOMAIN=localhost > .env && docker compose up -d --build` serves `wss://localhost` with
  Caddy's own local certificate, which your browser won't trust (`curl -k` to test it).
