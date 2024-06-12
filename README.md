# FuriOS initial setup

Initial setup program for FuriOS

## License

FuriOS initial setup is licensed under the GPLv3+.

## Getting the source

```sh
git clone https://github.com/FuriLabs/furios-initial-setup
cd furios-initial-setup
```

## Dependencies
On a Debian based system run

```sh
sudo apt-get -y install build-essential
sudo apt-get -y build-dep .
```

For an explicit list of dependencies check the `Build-Depends` entry in the
[debian/control][] file.

## Building

We use the meson (and thereby Ninja) build system for furios-initial-setup.  The quickest
way to get going is to do the following:

```sh
meson . _build
meson compile -C _build
```

## Running

You can run form the source tree:

```sh
_build/src/furios-initial-setup
```
