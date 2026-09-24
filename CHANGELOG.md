# Changelog

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
