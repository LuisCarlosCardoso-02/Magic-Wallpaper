# MagicWallpaper

A small wallpaper manager for Linux (made with the COSMIC desktop in mind) that sits on top of [mpvpaper](https://github.com/GhostNaN/mpvpaper).

mpvpaper does the hard part: it plays a video as your wallpaper. MagicWallpaper just takes the chore out of using it. You pick a video from a window, choose which monitors it should play on, and hit Apply. No more typing mpvpaper commands by hand, and it can bring your wallpapers back on its own when you log in.

## What it can do

- Pick videos with your system's file chooser and see them as thumbnails
- Keep its own copy of every video you add, so deleting the original later doesn't break your wallpaper
- Save several configs (a wallpaper, its mpv options, which monitors to use) and switch between them
- Work with more than one monitor, turning each one on or off per config
- Keep wallpapers running after you close the window. They only stop when you press "Stop all", run `magicwallpaper -stop`, or if mpvpaper itself quits because of an error
- Remember what it started, so you can stop wallpapers even after closing and reopening the app
- Start your wallpapers automatically when you log in

## What you need

| Needed for | Package |
|---|---|
| Running it (the backend, can't be skipped) | `mpvpaper` |
| Thumbnails | `ffmpeg` |
| Building it during install | `gcc`, `make`, `pkg-config` |
| The window | GTK 4 development files, **version 4.10 or newer** |

After installing you can remove `gcc` and `make` if you like. `mpvpaper` and `ffmpeg` have to stay.

On Pop!_OS / Ubuntu the build tools and GTK come from:

```
sudo apt install build-essential pkg-config libgtk-4-dev ffmpeg
```

mpvpaper isn't packaged everywhere, so you may need to build it from its own page.

## Installing

```
chmod +x Deploy.sh
./Deploy.sh
```

The script checks that everything above is installed (and tells you what's missing), builds the program and installs it **for your user only**, with no sudo. It sets up:

- the program at `~/.local/bin/magicwallpaper`
- an entry in your app menu, with an icon
- a shortcut on your Desktop
- a startup entry that runs `magicwallpaper -auto` when you log in

Options if you don't want everything:

```
./Deploy.sh --no-desktop-icon   # skip the Desktop shortcut
./Deploy.sh --no-autostart      # skip the startup entry
./Deploy.sh --nogui             # command line only, no GTK needed
```

Make sure `~/.local/bin` is in your `PATH` if you want to type `magicwallpaper` in a terminal. The menu and Desktop icons work either way.

## Using it

Open **MagicWallpaper** from your app menu or the Desktop icon, or run `magicwallpaper`.

1. Click **Add video...** and choose one or more videos. They get copied into the library.
2. Click a thumbnail to select it.
3. Tick the monitors you want, and change the mpv options if you like (the default is `no-audio loop`).
4. Click **Apply**.

You can close the window afterwards and the wallpaper keeps playing. Open it again whenever you want to change something or stop it.

Each config also has a "start automatically" checkbox. Configs with it ticked are what `-auto` starts at login.

## Command line

```
magicwallpaper            open the window
magicwallpaper -gui       open the window (same thing)
magicwallpaper -auto      start every config marked for autostart, then exit
magicwallpaper -stop      stop all running wallpapers
magicwallpaper -status    show what's running
magicwallpaper -add FILE  copy a video into the library
magicwallpaper -h         help
```

There's no text-based (console) interface, only the window and the commands above.

## Autostart

The startup entry is a standard XDG autostart file. Most desktops pick it up, but some window managers ignore it. If your wallpapers don't come back after logging in, add `magicwallpaper -auto` to your session's startup commands yourself. For example, `exec-once = magicwallpaper -auto` in Hyprland or `exec magicwallpaper -auto` in Sway.

## Where things are kept

Everything the program creates lives in `~/.local/share/MagicWallpaper/`:

```
wallpapers/   your video copies
thumbs/       thumbnails
config/       your saved configs
run/          what's currently running, plus mpvpaper logs
log.txt       the program's log
```

This folder is managed by the program, so moving or editing things in it by hand can break stuff. It's better to use the app to manage your wallpapers. If something looks wrong, `log.txt` is the first place to look.

## Good to know

- It plays one video per config, on every monitor you enabled in it. For a different video on each monitor, make one config per monitor and apply each one.
- Monitor names are read from `/sys/class/drm` (things like `HDMI-A-1` or `DP-2`). They normally match what mpvpaper expects, but on some setups they might not.

## License

MIT. Free to use, modify and share, as long as the copyright notice stays.
See the [LICENSE](LICENSE) file for the full text.

If you paid for this, you were probably scammed.

Enjoy :D

## Credits

Luis Carlos Pinheiro Cardoso ("02LSC")

GitHub: --------
LinkedIn: --------
