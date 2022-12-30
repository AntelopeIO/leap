## Description

The `chain_plugin` is a core plugin required to process and aggregate chain data on an Antelope node.

## Usage

```console
# config.ini
plugin = eosio::chain_plugin
[options]
```
```sh
# command-line
nodeos ... --plugin eosio::chain_plugin [operations] [options]
```

## Operations

These can only be specified from the `nodeos` command-line:

```console
Command Line Options for eosio::chain_plugin:
  --genesis-json arg                    File to read Genesis State from
  --genesis-timestamp arg               override the initial timestamp in the
                                        Genesis State file
  --print-genesis-json                  extract genesis_state from blocks.log
                                        as JSON, print to console, and exit
  --extract-genesis-json arg            extract genesis_state from blocks.log
                                        as JSON, write into specified file, and
                                        exit
  --print-build-info                    print build environment information to
                                        console as JSON and exit
  --extract-build-info arg              extract build environment information
                                        as JSON, write into specified file, and
                                        exit
  --force-all-checks                    do not skip any validation checks while
                                        replaying blocks (useful for replaying
                                        blocks from untrusted source)
  --disable-replay-opts                 disable optimizations that specifically
                                        target replay
  --replay-blockchain                   clear chain state database and replay
                                        all blocks
  --hard-replay-blockchain              clear chain state database, recover as
                                        many blocks as possible from the block
                                        log, and then replay those blocks
  --delete-all-blocks                   clear chain state database and block
                                        log
  --truncate-at-block arg (=0)          stop hard replay / block log recovery
                                        at this block number (if set to
                                        non-zero number)
  --terminate-at-block arg (=0)         terminate after reaching this block
                                        number (if set to a non-zero number)
  --snapshot arg                        File to read Snapshot State from

```

## Options

These can be specified from both the `nodeos` command-line or the `config.ini` file:

```console
Config Options for eosio::chain_plugin:
  --blocks-dir arg (="blocks")          the location of the blocks directory
                                        (absolute path or relative to
                                        application data dir)
  --protocol-features-dir arg (="protocol_features")
                                        the location of the protocol_features
                                        directory (absolute path or relative to
                                        application config dir)
  --checkpoint arg                      Pairs of [BLOCK_NUM,BLOCK_ID] that
                                        should be enforced as checkpoints.
  --wasm-runtime runtime (=eos-vm-jit)  Override default WASM runtime (
                                        "eos-vm-jit", "eos-vm")
                                        "eos-vm-jit" : A WebAssembly runtime
                                        that compiles WebAssembly code to
                                        native x86 code prior to execution.
                                        "eos-vm" : A WebAssembly interpreter.

  --profile-account arg                 The name of an account whose code will
                                        be profiled
  --abi-serializer-max-time-ms arg (=15)
                                        Override default maximum ABI
                                        serialization time allowed in ms
  --chain-state-db-size-mb arg (=1024)  Maximum size (in MiB) of the chain
                                        state database
  --chain-state-db-guard-size-mb arg (=128)
                                        Safely shut down node when free space
                                        remaining in the chain state database
                                        drops below this size (in MiB).
  --signature-cpu-billable-pct arg (=50)
                                        Percentage of actual signature recovery
                                        cpu to bill. Whole number percentages,
                                        e.g. 50 for 50%
  --chain-threads arg (=2)              Number of worker threads in controller
                                        thread pool
  --contracts-console                   print contract's output to console
  --deep-mind                           print deeper information about chain
                                        operations
  --actor-whitelist arg                 Account added to actor whitelist (may
                                        specify multiple times)
  --actor-blacklist arg                 Account added to actor blacklist (may
                                        specify multiple times)
  --contract-whitelist arg              Contract account added to contract
                                        whitelist (may specify multiple times)
  --contract-blacklist arg              Contract account added to contract
                                        blacklist (may specify multiple times)
  --action-blacklist arg                Action (in the form code::action) added
                                        to action blacklist (may specify
                                        multiple times)
  --key-blacklist arg                   Public key added to blacklist of keys
                                        that should not be included in
                                        authorities (may specify multiple
                                        times)
  --sender-bypass-whiteblacklist arg    Deferred transactions sent by accounts
                                        in this list do not have any of the
                                        subjective whitelist/blacklist checks
                                        applied to them (may specify multiple
                                        times)
  --read-mode arg (=speculative)        Database read mode ("speculative",
                                        "head", "read-only", "irreversible").
                                        In "speculative" mode: database
                                        contains state changes by transactions
                                        in the blockchain up to the head block
                                        as well as some transactions not yet
                                        included in the blockchain.
                                        In "head" mode: database contains state
                                        changes by only transactions in the
                                        blockchain up to the head block;
                                        transactions received by the node are
                                        relayed if valid.
                                        In "read-only" mode: (DEPRECATED: see
                                        p2p-accept-transactions &
                                        api-accept-transactions) database
                                        contains state changes by only
                                        transactions in the blockchain up to
                                        the head block; transactions received
                                        via the P2P network are not relayed and
                                        transactions cannot be pushed via the
                                        chain API.
                                        In "irreversible" mode: database
                                        contains state changes by only
                                        transactions in the blockchain up to
                                        the last irreversible block;
                                        transactions received via the P2P
                                        network are not relayed and
                                        transactions cannot be pushed via the
                                        chain API.

  --api-accept-transactions arg (=1)    Allow API transactions to be evaluated
                                        and relayed if valid.
  --validation-mode arg (=full)         Chain validation mode ("full" or
                                        "light").
                                        In "full" mode all incoming blocks will
                                        be fully validated.
                                        In "light" mode all incoming blocks
                                        headers will be fully validated;
                                        transactions in those validated blocks
                                        will be trusted

  --disable-ram-billing-notify-checks   Disable the check which subjectively
                                        fails a transaction if a contract bills
                                        more RAM to another account within the
                                        context of a notification handler (i.e.
                                        when the receiver is not the code of
                                        the action).
  --maximum-variable-signature-length arg (=16384)
                                        Subjectively limit the maximum length
                                        of variable components in a variable
                                        legnth signature to this size in bytes
  --trusted-producer arg                Indicate a producer whose blocks
                                        headers signed by it will be fully
                                        validated, but transactions in those
                                        validated blocks will be trusted.
  --database-map-mode arg (=mapped)     Database map mode ("mapped", "heap", or
                                        "locked").
                                        In "mapped" mode database is memory
                                        mapped as a file.
                                        In "heap" mode database is preloaded in
                                        to swappable memory and will use huge
                                        pages if available.
                                        In "locked" mode database is preloaded,
                                        locked in to memory, and will use huge
                                        pages if available.

  --eos-vm-oc-cache-size-mb arg (=1024) Maximum size (in MiB) of the EOS VM OC
                                        code cache
  --eos-vm-oc-compile-threads arg (=1)  Number of threads to use for EOS VM OC
                                        tier-up
  --eos-vm-oc-enable                    Enable EOS VM OC tier-up runtime
  --enable-account-queries arg (=0)     enable queries to find accounts by
                                        various metadata.
  --max-nonprivileged-inline-action-size arg (=4096)
                                        maximum allowed size (in bytes) of an
                                        inline action for a nonprivileged
                                        account
  --transaction-retry-max-storage-size-gb arg
                                        Maximum size (in GiB) allowed to be
                                        allocated for the Transaction Retry
                                        feature. Setting above 0 enables this
                                        feature.
  --transaction-retry-interval-sec arg (=20)
                                        How often, in seconds, to resend an
                                        incoming transaction to network if not
                                        seen in a block.
  --transaction-retry-max-expiration-sec arg (=120)
                                        Maximum allowed transaction expiration
                                        for retry transactions, will retry
                                        transactions up to this value.
  --transaction-finality-status-max-storage-size-gb arg
                                        Maximum size (in GiB) allowed to be
                                        allocated for the Transaction Finality
                                        Status feature. Setting above 0 enables
                                        this feature.
  --transaction-finality-status-success-duration-sec arg (=180)
                                        Duration (in seconds) a successful
                                        transaction's Finality Status will
                                        remain available from being first
                                        identified.
  --transaction-finality-status-failure-duration-sec arg (=180)
                                        Duration (in seconds) a failed
                                        transaction's Finality Status will
                                        remain available from being first
                                        identified.
  --block-log-retain-blocks arg         if set, periodically prune the block
                                        log to store only configured number of
                                        most recent blocks

```

## Dependencies

None
