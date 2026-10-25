# Changelog

## [1.0.0-rc.20](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.19...v1.0.0-rc.20) (2026-10-25)


### Features

* run tiles, output and NMOS caps at MXL_GRAIN_RATE ([78a0ec3](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/78a0ec39ddcf096aa7954559066afdec2516a820))


### Bug Fixes

* encode the mosaic with x264 preset superfast ([78a0ec3](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/78a0ec39ddcf096aa7954559066afdec2516a820))

## [1.0.0-rc.19](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.18...v1.0.0-rc.19) (2026-10-25)


### Bug Fixes

* **nmos:** advertise the pod's DNS name as the Node's host name ([#39](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/39)) ([7b23abf](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/7b23abf21d8e7f50670eaf96be28ea6569952d9e))

## [1.0.0-rc.18](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.17...v1.0.0-rc.18) (2026-10-23)


### Bug Fixes

* **nmos:** advertise each control href once ([#36](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/36)) ([9099412](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/9099412e180876de37be4ad5274094d7a8086362))

## [1.0.0-rc.17](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.16...v1.0.0-rc.17) (2026-10-23)


### Bug Fixes

* **nmos:** read a flow definition's escaped media type ([#35](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/35)) ([62dab86](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/62dab8644ab8c74b50fa007530eb647c72b669dd))

## [1.0.0-rc.16](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.15...v1.0.0-rc.16) (2026-10-22)


### Dependencies

* **compositor:** move onto go-mxl 1.1.0-rc.7 ([#33](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/33)) ([fb93af8](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/fb93af851a07987408deec1fa38b889e02f25559))

## [1.0.0-rc.15](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.14...v1.0.0-rc.15) (2026-10-16)


### Bug Fixes

* **nmos:** pace a tile at the rate of the flow connected to it ([#29](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/29)) ([ab9b656](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/ab9b656cabbc66979d1abd85299efba861d38503))

## [1.0.0-rc.14](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.13...v1.0.0-rc.14) (2026-09-28)


### Features

* encode the mosaic at 25 fps ([#30](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/30)) ([6b1543a](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/6b1543a86f8be0e561d1900fbf6a371c06c38550))

## [1.0.0-rc.13](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.12...v1.0.0-rc.13) (2026-09-28)


### Bug Fixes

* show every grain of a flow, not every grain a tick samples ([#27](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/27)) ([c8c14ef](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/c8c14ef790c9df7545f47508e83369871daf6f9d))

## [1.0.0-rc.12](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.11...v1.0.0-rc.12) (2026-09-27)


### Features

* **nmos:** keep tile connections across a restart ([#25](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/25)) ([b38dedc](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/b38dedc8724a8490fef227d1dd6afaae75a8dc97))

## [1.0.0-rc.11](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.10...v1.0.0-rc.11) (2026-09-25)


### Features

* connect a tile to a flow in any MXL domain on the node ([#22](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/22)) ([97a09ef](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/97a09ef1bdd278b854d2170513dacee3df838a1b))


### Build System

* survive a slow dependency host when building libnvnmos ([#24](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/24)) ([3a078de](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/3a078de9b7d3f133968dfcc9b6bd794b962fb93e))

## [1.0.0-rc.10](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.9...v1.0.0-rc.10) (2026-09-24)


### Bug Fixes

* advertise the Node at the address it was given ([#20](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/20)) ([cde0c04](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/cde0c0479a0a0439fbaa7dd4fc7250f2f50cf6ac))

## [1.0.0-rc.9](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.8...v1.0.0-rc.9) (2026-09-23)


### Features

* connect tiles over NMOS as BCP-007-03 MXL receivers ([849a311](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/849a311f7b200cc8165a901ec79159a3537b5895))


### Bug Fixes

* show a switched tile black until its flow arrives, and refuse flows that are not v210 video ([849a311](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/849a311f7b200cc8165a901ec79159a3537b5895))


### Build System

* publish libnvnmos as its own pinned image, built one run at a time with its dependency layer cached ([849a311](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/849a311f7b200cc8165a901ec79159a3537b5895))

## [1.0.0-rc.8](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.7...v1.0.0-rc.8) (2026-08-27)


### Miscellaneous

* drop the audio preview ([#16](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/16)) ([73229ce](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/73229ce5d688515b3fabdb0251cb6ff810b63bb4))

## [1.0.0-rc.7](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.6...v1.0.0-rc.7) (2026-08-21)


### Features

* **encode:** cap the bitrate where the path to a viewer is narrow ([#14](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/14)) ([e5d19a3](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/e5d19a38c8ac694729b070738040050baa240587))

## [1.0.0-rc.6](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.5...v1.0.0-rc.6) (2026-08-21)


### Bug Fixes

* **encode:** drop B-frames so WebRTC can carry the mosaic ([#12](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/12)) ([880bc5e](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/880bc5ec1c7b47fe7193ee8c55174bceb312f86b))

## [1.0.0-rc.5](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.4...v1.0.0-rc.5) (2026-08-09)


### Bug Fixes

* **audio-preview:** decay the meter envelope per sample ([#8](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/8)) ([bf73b27](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/bf73b270ece32325d53f7591a31ad0e73d72bdec))

## [1.0.0-rc.4](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.3...v1.0.0-rc.4) (2026-08-09)


### Bug Fixes

* **audio-preview:** report a level a caller can meter ([#6](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/6)) ([42eb5b5](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/42eb5b5312d02fa372fa90c6b087e09e51bdddc1))

## [1.0.0-rc.3](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.2...v1.0.0-rc.3) (2026-08-09)


### Features

* **audio-preview:** publish a selectable stereo pair from any flow ([#4](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/4)) ([f62d463](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/f62d463c0b2465ae4ccd898415237920510a6319))

## [1.0.0-rc.2](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.1...v1.0.0-rc.2) (2026-08-04)


### Dependencies

* move onto go-mxl 1.0.0-rc.12 ([#2](https://github.com/qvest-digital/dmf-mf-mxl-compositor/issues/2)) ([e1ab6d1](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/e1ab6d1a035863f540f5cc060dcc4535c22f59bb))

## [1.0.0-rc.1](https://github.com/qvest-digital/dmf-mf-mxl-compositor/compare/v1.0.0-rc.0...v1.0.0-rc.1) (2026-08-03)


### Features

* seed the MXL compositor media function ([1bb0437](https://github.com/qvest-digital/dmf-mf-mxl-compositor/commit/1bb0437ab23d97e58e569c57389eb52588f46ebd))
