# Validation logs

These logs describe the accepted clean Boost and selector, not the rejected
Overdrive. Debug/Release/ASan+UBSan each pass 244 tests. The separate focused
logs cover 8 MC402 tests, 4 routing tests and 12 TC BLD regressions.

- [Debug suite](debug-tests.txt), [Release suite](release-tests.txt).
- [ASan/UBSan suite](sanitizer-tests.txt), [TSan handoff](tsan-tests.txt).
- [MC402 focused](focused.txt), [routing](routing.txt), [TC BLD](tc-regressions.txt).

Full Xcode build transcripts (`debug-build.log.gz`, `release-build.log.gz`,
`plugin-build-final.log.gz`) are excluded generated outputs. Build outcomes
remain in [validation.json](../validation.json); [validate.sh](../validate.sh)
writes fresh full logs under its temporary build directory.

Products were built for Debug arm64, without installing plugins; that build
covered all three targets. The standalone was launched from
the isolated `/tmp/mc402-boost-v1/products` directory. No visual inspection or
audio audition was automated.

Strict warnings passed for new production DSP/test sources. Applying the same
warning-as-error set to all legacy code initially exposed existing unrelated
warnings; those sources were not altered. Xcode cache sandbox failures were
resolved by authorized developer-service access before successful builds.

Every validation-script component was executed during this task. The complete
`validate.sh` wrapper was syntax-checked, not redundantly rerun after the
successful component runs. Archived Overdrive bridge/R2 compilation checks
were separate; no Overdrive measurements were rerun.
