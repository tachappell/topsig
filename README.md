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

## Configuring TopSig

Configuration options can be passed to TopSig through command-line arguments, configuration files or both.

When configuration options are passed through the command line, they are prefixed with a `-` and followed by the value that the configuration option is to be set to. For instance, to create a signature file named `output.sig` from the files inside a directory named `collections`, the following invocation would be used:
```
./topsig index -target-path collection -signature-path output.sig
```

Configuration options can also be passed through configuration files, which have one configuration option per line (without the `-` prefix), followed by an `=`, followed by the value that the configuration option is to be set to.

By default, TopSig will read configuration information from a file named `config.txt` in the working directory TopSig is invoked from, providing it exists. Additional configuration files can also be specified through the `-config` configuration option. So, for example, the above invocation could also be reproduced by running TopSig with just the invocation mode:
```
./topsig index
```
...if the following `config.txt` file is also present:
```
target-path = collection
signature-path = output.sig
```
If the settings are in a different file, e.g. `settings.cfg`, the path will need to be provided through `-config`:
```
./topsig index -config settings.cfg
```
Using this option you can keep different configuration files around to make keeping track of the settings used to obtain different results easier.

## Configuration options

### Collection options

These options provide information about the data collection used in indexing. These options are used when TopSig is invoked in the `index` and `termstats` modes.

#### `-target-path (path)`

(tbc..)
