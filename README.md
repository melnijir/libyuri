# libyuri - NDI 6 / Trixie Fork

This is a fork of [libyuri](https://github.com/melnijir/libyuri) with support for **NDI SDK 6** and **Raspberry Pi OS Trixie (Debian 13)**.

## What is libyuri?

libyuri (yuri2) is a powerful, modular multimedia processing framework written in C++. It provides a flexible pipeline-based architecture for video/audio capture, processing, and output with support for numerous formats and protocols.

## Fork Changes

This fork (`ndi6-trixie-support` branch) adds the following:

### NDI 6 SDK Support
- Updated `FindNDI.cmake` to detect NDI SDK version 6.0
- Added conditional NDI module building based on SDK availability

### FFmpeg 7 Compatibility
- Fixed deprecated `channel_layout`/`channels` API usage in favor of `ch_layout`
- Updated `avoutput` and `rawavfile` modules for FFmpeg 7.x compatibility

### Raspberry Pi OS Trixie (Debian 13)
- Tested and verified on Raspberry Pi OS Trixie
- Compatible with Linux 6.12+ kernel on aarch64

### Other Fixes
- Fixed webserver module build (jsoncpp detection order)
- Changed ESC key behavior in SDL2Window

## Building

### Prerequisites

```bash
sudo apt-get install -y \
    build-essential cmake git pkg-config \
    libsdl2-dev libboost-all-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libjpeg-dev
```

For NDI support, install the [NDI SDK](https://ndi.video/download-ndi-sdk/).

### Build

```bash
git clone -b ndi6-trixie-support https://github.com/bsuecm/libyuri.git
cd libyuri
mkdir build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DNDI_INCLUDE_DIRS=/usr/local/include \
    -DNDI_LIBRARIES=/usr/local/lib/libndi.so.6
make -j$(nproc)
sudo make install
```

## Credits

### Original Authors
- **Zdenek Travnicek** ([@v154c1](https://github.com/v154c1)) - Original yuri2 author and primary developer
- **Jiri Melnikov** ([@melnijir](https://github.com/melnijir)) - Maintainer of the upstream fork

### Fork Contributors
- **Artem Aleksashenko** ([@bsuecm](https://github.com/bsuecm)) - NDI 6 and Trixie support
- **Claude Opus 4.5** (Anthropic) - AI pair programming assistant

## License

This project maintains the same license as the original libyuri project.

## Links

- **This fork**: https://github.com/bsuecm/libyuri
- **Upstream fork**: https://github.com/melnijir/libyuri
- **Original project**: https://github.com/v154c1/libyuri
