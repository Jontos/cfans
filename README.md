cfans - a lightweight fan control daemon for GNU/Linux
======================================================

`cfans` is a lightweight, userspace daemon written in pure C and configured with JSON that allows custom control of fan speeds via the [sysfs(5)](https://man.archlinux.org/man/sysfs.5.en) interface.

Key Features
------------
- **Custom sensors:** `Max` sensor uses maximum temperature of selected sensors. `File` sensors reads temperature from an arbitrary file. Apply an `offset` to adjust sensor values.
- **Curve options:** Configurable `hysteresis` and `response time` settings to prevent rapid fan speed changes.
- **Text-based configuration:** Version control friendly, easy to backup.
- **Systemd integration:** Designed to be run in the background as a systemd service.
- **Security:** Service runs as a separate user with dropped permissions, utilising udev rules to allow access to the hwmon interface.

Build & Install
---------------
Requires `gcc`, `make`, `systemd-libs` and `cjson`.
Build with:
```bash
git clone https://github.com/jontos/cfans.git
cd cfans
make
```
and install with:
```bash
sudo make install
```
Currently only tested on Arch Linux.

Configuration
-------------
By default `cfans` reads `/etc/cfans/config.json` for configuration. A custom location can be supplied with the `-c` command line flag. See [config.json.example](config.json.example) for an example configuration.

Project Status & Roadmap
------------------------
This project is currently in active development.

### Planned features/TODO
- [ ] Automatic hwmon sensor and PWM control detection.
- [ ] Sensor value averaging.
- [ ] Add a PKGBUILD and publish to the AUR.
- [ ] Implement IPC interface to allow external control/configuration.

License
-------
This project is licensed under the GNU General Public License v3 (GPLv3) - see the [LICENSE](LICENSE) file for details.
