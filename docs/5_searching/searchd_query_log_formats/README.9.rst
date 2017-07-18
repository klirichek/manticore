``searchd`` query log formats
-----------------------------

In version 2.0.1-beta and above two query log formats are supported.
Previous versions only supported a custom plain text format. That format
is still the default one. However, while it might be more convenient for
manual monitoring and review, but hard to replay for benchmarks, it only
logs *search* queries but not the other types of requests, does not
always contain the complete search query data, etc. The default text
format is also harder (and sometimes impossible) to replay for
benchmarking purposes. The new ``sphinxql`` format alleviates that. It
aims to be complete and automatable, even though at the cost of brevity
and readability.
