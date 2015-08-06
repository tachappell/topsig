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

This option points to the collection. This can either be a file or a directory. If it is a directory, every file within the directory will be processed.

#### `-target-path-2 (path)`

This option points to a secondary collection file / directory that will be processed after the file pointed to by `-target-path` and added to the same signature file. Any number of additional collection files / directories can be added through the `-target-path-3` `-target-path-4` etc. options.

#### `-target-format (format)`

This option specifies the format that the collection files are in (if a directory was passed to `-target-path`, this option specifies the format of the files inside that directory).

Valid options include:
- `file` (default): File contains a single document
- `tar`: File is a TAR format archive containing multiple files, each of which is a document
- `wsj`: File is a TREC format SGML file consisting of multiple documents delimited with `<DOC> </DOC>` tags and with document filenames specified through `<DOCNO> </DOCNO>` tags.
- `warc`: File is a WARC format collection file, as used in ClueWeb09 and ClueWeb12, and consists of multiple documents, with document filenames specified with `WARC-TREC-ID:` labels.
- `newline`: File is a text file, with each line considered to be a separate document. Files are named by line number.

#### `-target-format-compression (mode)`

This option specifies the compression mode used to compress the files pointed to by `-target-path`.

Valid options include:
- `none` (default): File is uncompressed
- `gz`: File is compressed with gzip
- `bz2`: File is compressed with bzip2

#### `-target-format-filter (filter)`

This option specifies a filter that the documents are run through when determining which parts of the document consist of text (which should be indexed) and which parts consist of markup (which should be ignored).

Valid options include:
- `none` (default): No filter is used
- `xml`: A basic filter that strips XML tags is used. This is useful for cleaning up collections such as Wikipedia, which contain a lot of formatting information, most of which is not useful.

#### `-docid-format (format)`

When documents are indexed into signatures, they are given an identifier based on the original document that can be used to identify this document later on. While certain formats (`-target-format` `wsj` and `warc`) explicitly provide the document ID, when the format is `file` or `tar` this option selects how TopSig will determine the document ID.

Valid options include:
- `path` (default): The path to the file, including the file extension. For TAR archives this is the full path stored in the TAR; for files/directories, this is the path as provided to TopSig. For instance, if `-target-path` is specified as `collection` and `collection` is a directory containing files `1.txt`, `2.txt` and `3.txt`, the document IDs of those files will be stored as `collection/1.txt`, `collection/2.txt`, `collection/3.txt`.
- `basename`: The directory path (everything up to the last directory separator) and the extension will be stripped- with the file structure used in the previous example, this would mean document IDs of `1`, `2` and `3`.
- `basename.ext`: The directory path will be stripped from the filename, but the extension will be kept: `1.txt`, `2.txt`, `3.txt`.
- `xmlfield`: The filename will be ignored, and the document ID instead pulled out of an XML field inside the folder. The XML field in question must be specified through the `-xml-docid-field` option. For example, if a document is indexed with the options `-docid-format xmlfield -xml-docid-format docname` the document ID will be pulled from between the `<docname> </docname>` tags in the file.

#### `-split-type (mode)`

This option specifies that TopSig is to index documents that are too long into multiple signatures. This can be desirable for two reasons:

* Signature quality tends to deteriorate when the documents contain too many terms for the signature size. Splitting up documents helps to avoid this - documents that are larger than the specified threshold will be split up and searches will continue to work.
* For certain modes of retrieval it is beneficial to be able to operate at the passage or sentence level, rather than at whole document level. TopSig records the starting and ending offsets within the original file of each signature making focused search possible with splitting.

Valid options include:
- `none` (default): No splitting takes place
- `hard`: Documents are split immediately upon reaching the term threshold specified with the `-split-max` option (default: 512)
- `sentence`: Documents are split either upon reaching the `-split-max` threshold, or after reaching the `-split-min` threshold (default: 256) and encountering a full stop character (`.`).

