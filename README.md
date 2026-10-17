# Harbor Cutlist

A C++17 REST service for planning cuts from stock bars, using first-fit decreasing.
This heuristic is deterministic but does not promise the mathematically optimal
packing. Synthetic millimetres and materials; no persistence, env, credentials,
external dependencies, private edges or raw network permission on Dev.

WASI runs once per request: raw HTTP on stdin, JSON on stdout, logs on stderr.
The native JSON adapter always returns HTTP 200; inspect the `ok` field for business
validation. Local `--serve` uses the same handler on http://127.0.0.1:8240.

    mkdir -p .local
    g++ -std=c++17 -O1 src/main.cpp -o .local/cutlist
    .local/cutlist --serve

GET `/plan/6000/1200,1800,2400`: one bar, 5400 mm material, 600 mm waste.
GET `/plan/6000/4500,3000,3000,1500`: two bars, zero waste.
POST `/plan`, Content-Type text/plain, body `6000\n1200,1800,2400` does the same.
Rejects oversized pieces, invalid integers, incomplete lists and >100 pieces.
`/health` identifies the service and release. The service is intentionally public.

Release 1 ignores kerf. Release 2 will allow 3 mm per requested piece, including
the last cut on each stock bar; the first example then has 591 mm waste.
