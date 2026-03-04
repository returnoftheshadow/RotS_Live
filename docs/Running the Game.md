# Running the Game

On the <rotsmud.org> server the live, coding and building ports are run under `systemctl`.

**Service Names**:

- `rotslive.service`
- `rotsbuilding.service`
- `rotscoding.service`

## Live Port

The live port is configured to use port `3791` and the working directory on the
server is: `/rots/live-default3791`. Here you'll find the standard folder
layout for the game as generated from `make setup`. The only difference is
there is a `autorun3791` shell script that is used to perform the necessary
steps to run the game on the live port and backup all the files.

```text
-- /rots/live-default3791
   - backups
   - bin
   - core
   - levgen
   - lib
   - log
   - src
   - www
```

All source files should be SFTPed and compiled in the `src` folder. Inside
the `src` folder there is a `backup` folder that i first move the previous code
base into after running `make clean` (this is done to get rid of any unwanted
files that may be left over from the previous code base). After the backup is
made, the new code base is copied into the `src` folder and then compiled
(`make all -j 6`). However after compiling the game the changes won't take
affect until after the game is restarted. This can be accomplished by running
`shutdown reboot` in the game or running `systemctl restart rotslive` on the
server.

## Building Port

The building port is configured to use port `4802` and the working directory on
the server is: `/rots/dev-building4802`. Here you'll find the same folder
layout as the live port but with a different `autorun4802` shell script that is
used to perform the necessary steps to run the game on the building port and
backup all the files.

The same process should be used as live for transferring the code base,
compiling and restarting the game. The only difference is that you would run
`systemctl restart rotsbuilding` on the server to restart the building port.

## Coding Port

The coding port is configured to use port `4810` and the working directory on
the server is: `/rots/dev-coding4810`. Here you'll find the same folder layout
as the live port but with a different `autorun4810` shell script that used to
run the game. There is no backups on the port because it's just meant for
compiling source code and testing changes.

The same process should be used as live for transferring the code base,
compiling and restarting the game. The only difference is that you would run
`systemctl restart rotscoding` on the server to restart the coding port.

## Restoring Characters

To restore a character from the live port you'll need to do the following steps:

- Navigate to the live port's working directory: `/rots/live-default3791`
- Next change directories to `backups/daily`

Inside this directory you'll find the following folders:

```text
-- backups/daily
   - exploits/
   - players/
   - plrobjs/
```

> [!TIP] These files are backed up daily and kept for 30 days.

- If you want to restore a characters exploits use the `exploits` folder, if
you want to restore a character's player file use the `players` folder and if
you want to restore a character's object file use the `plrobjs` folder.
- Once you have identified the file you want to restore, copy it to your home
directory on the server (typically under a temp folder). For example:

```bash
cp -v /rots/live-default3791/backups/daily/players/players-2026-02-01.tar.gz ~/temp/
```

- Next, navigate to the temporary directory and extract the file:

```bash
cd ~/temp
tar -xvzf players-2026-02-01.tar.gz ./
```

> [!INFO]+ This will extract the files to the current directory.
> With the structure of the original file. For example, if you extracted a
> player file it would be extracted to `./rots/live-default3791/lib/players`
> and if you extracted an exploits file it would be extracted to
> `./rots/live-default3791/lib/exploits`.

- Make any modifications to the file if necessary and then copy it back to the original:

```bash
sudo cp -v rots/live-default3791/lib/players/A-E/aahz.32 /rots/live-default3791/lib/players/A-E/
```

> [!NOTE] Change the directories according to the file you are restoring.
