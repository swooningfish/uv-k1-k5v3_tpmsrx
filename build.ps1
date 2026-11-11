docker run --rm -v "${PWD}:/work" -w /work tpmsrx bash -lc "bash ./build.sh && bash ./test/run_tests.sh"
pause