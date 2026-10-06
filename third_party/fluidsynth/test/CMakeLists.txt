# Olduvai: FluidSynth's unit tests are not vendored.  Upstream adds this
# directory unconditionally, and its tests would register in Olduvai's ctest
# run without ever being built.  See ../OLDUVAI-VENDORING.md.
