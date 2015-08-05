# TopSig
TopSig is a signature indexing and retrieval platform based around storing compact document signatures and enabling comparisons and retrieval using these signatures.

## Usage
```
./topsig invocation-mode [options...]
```

The invocation mode specifies the overall operation TopSig is being instructed to run. The options are used to provide all other configuration information, including the paths to input and output files.

## Invocation modes

* `index`: When invoked in this mode, TopSig will read in the collection specified through `-target-path`, generate document signatures for each document in the collection and write results to the signature file specified with `-signature-path`.
* `query`: When invoked in this mode, TopSig will take a single text query through the configuration option `-query-text` and will use this query to search the signature file specified with `-signature-path`.
* `topic`: When invoked in this mode, TopSig will take a file containing one or more queries through the configuration option `-topic-path` and will search the collection specified through `-signature-path` with these queries.
* `termstats`: When invoked in this mode, TopSig will read in the collection specified through `-target-path` and will write out term statistics to the path specified through `-termstats-path`. These term statistics can then be provided to TopSig when run in other invocation modes (such as `index`, `query` and `topic`) with the `-termstats-path` option.
* `create-issl`: When invoked in this mode, TopSig will read in the signature file specified through `-signature-path` and write out an inverted signature slice list (ISSL) table to the path specified through `-issl-path`.
* `search-issl`: When invoked in this mode, TopSig will read in the ISSL table and signature file specified with the `-issl-path` and `-signature-path` options and perform accelerated pairwise searches against signatures in this collection.

## Configuration options

Configuration options can be passed to TopSig through command-line arguments, configuration files or both.
