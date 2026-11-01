# AI Usage Disclosure

## Purpose

AI tools may be used to support learning and development throughout the HTTP/1.1 Server and Caching Proxy project.

## Intended Uses

- Clarifying HTTP/1.1 concepts, socket programming, caching, and concurrency.
- Explaining compiler errors and helping debug code written by the team.
- Reviewing team-written code for correctness, readability, and security concerns.
- Suggesting test cases for HTTP requests, response handling, proxy behavior, and cache validation.
- Helping interpret benchmark results and improve project documentation.

## Development and Verification

All submitted implementation work will be understood, reviewed, and tested by the team. AI suggestions will be checked against the project requirements and verified through local tests.

## Academic Integrity

The team is responsible for the final implementation and must follow the assignment's rules regarding AI assistance. AI-generated suggestions will not be represented as independently written work when disclosure is required.

## Record of Assistance

The team will update this document if the actual use of AI tools differs materially from the intended uses described above.

### Sipra — Forward Proxy Validation, Revalidation, Testing and Benchmarks

- **Explanations & Protocol Guidance**: Clarified RFC 7232 weak vs strong ETag comparison rules (§3.1), HTTP-date parsing requirements (§3.3), and 304 Not Modified cache update semantics in forward proxies.
- **Architecture & Code Review**: Reviewed interface boundaries for `cache_validator.h` and `cache_validator.c` to prevent touching or regressing Akshat's LRU cache and TCP forwarding code.
- **Test Generation**: Structured the non-Python automated test plan (`tests/proxy/test_validation.sh`) and mock origin server (`tests/proxy/mock_origin.c`) covering all 15 required validation test scenarios.
- **Benchmark Scripts**: Guided the implementation of the high-precision multithreaded C benchmark suite (`benchmarks/benchmark_validation.c`) using raw POSIX sockets and `clock_gettime(CLOCK_MONOTONIC)`.
- **Documentation**: Assisted in formatting and structuring the verified implementation details, measured benchmark tables, and testing results in `README.md`.
