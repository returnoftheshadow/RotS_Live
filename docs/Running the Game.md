# Running the Game

The live, building, and coding ports on `<rotsmud.org>` run as `systemd`
services. Use `systemctl` to start, stop, or restart each port:

- `rotslive.service`
- `rotsbuilding.service`
- `rotscoding.service`

## Port Overview

| Purpose  | Port | Working Directory           | Autorun Script | Restart Command                         |
|----------|------|-----------------------------|----------------|-----------------------------------------|
| Live     | 3791 | `/rots/live-default3791`    | `autorun3791`  | `systemctl restart rotslive`            |
| Building | 4802 | `/rots/dev-building4802`    | `autorun4802`  | `systemctl restart rotsbuilding`        |
| Coding   | 4810 | `/rots/dev-coding4810`      | `autorun4810`  | `systemctl restart rotscoding`          |

Each directory follows the structure created by `make setup`:

```text
-- /rots/<port-directory>
   - backups
   - bin
   - core
   - levgen
   - lib
   - log
   - src
   - www
```

### Deploying Code to Any Port

1. Upload the source via SFTP to the appropriate `src` directory.
2. In `src`, move the previous code base into the `backup/` folder after running `make clean` to avoid stale files.
3. Copy the new code into `src` and compile with `make all -j6`.
4. Restart the port so the changes take effect:
   - In-game: `shutdown reboot`
   - Or via shell: `systemctl restart <service-name>`

> [!NOTE] The coding port does not maintain backups; treat it as a scratch space for compilation and testing.

## Restoring Characters (Live Port)

1. Change to the live working directory: `cd /rots/live-default3791`.
2. Enter the daily backups: `cd backups/daily`. Within this directory you will find:

```text
-- backups/daily
   - exploits/
   - players/
   - plrobjs/
```

> [!TIP] Daily backups are retained for 30 days.

1. Choose the correct archive:
   - `exploits/` for exploit logs
   - `players/` for player files
   - `plrobjs/` for player objects
2. Copy the desired archive to a temporary working directory in your home folder:

```bash
cp -v /rots/live-default3791/backups/daily/players/players-2026-02-01.tar.gz ~/temp/
```

1. Extract the archive inside the temp directory:

```bash
cd ~/temp
tar -xvzf players-2026-02-01.tar.gz ./
```

> [!INFO] Files extract relative to the original tree (e.g., `./rots/live-default3791/lib/players`).

1. Review or modify the restored files as needed, then copy them back into place:

```bash
sudo cp -v rots/live-default3791/lib/players/A-E/aahz.32 /rots/live-default3791/lib/players/A-E/
```

> [!IMPORTANT] Adjust the destination path to match the file type you are restoring.
