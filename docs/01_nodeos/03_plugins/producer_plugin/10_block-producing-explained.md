---
content_title: Block Production Explained
---

For simplicity of the explanation let's consider the following notations:

* `r` = `producer_repetitions = 12` (hard-coded value)
* `m` = `max_block_cpu_usage` (on-chain consensus value)
* `u` = `max_block_net_usage` (on-chain consensus value)
* `t` = `block-time`
* `e` = `produce-block-offset-ms` (nodeos configuration)
* `w` = `block-time-interval = 500ms` (hard-coded value)
* `a` = `produce-block-early-amount = w - (w - (e / r)) = e / r ms` (how much to release each block of round early by)
* `l` = `produce-block-time = t - a`
* `p` = `produce block time window = w - a` (amount of wall clock time to produce a block)
* `c` = `billed_cpu_in_block = minimum(m, w - a)`
* `n` = `network tcp/ip latency`
* `h` = `block header validation time ms`

Peer validation for similar hardware/version/config will be <= `m`

**Let's consider the example of the following two BPs and their network topology as depicted in the below diagram**

```
         +------+     +------+       +------+     +------+
      -->| BP-A |---->| BP-A |------>| BP-B |---->| BP-B |
         +------+     | Peer |       | Peer |     +------+
                      +------+       +------+
```

`BP-A` will send block at `l` and, `BP-B` needs block at time `t` or otherwise will drop it.

If `BP-A`is producing 12 blocks as follows `b(lock) at t(ime) 1`, `bt 1.5`, `bt 2`, `bt 2.5`, `bt 3`, `bt 3.5`, `bt 4`, `bt 4.5`, `bt 5`, `bt 5.5`, `bt 6`, `bt 6.5` then `BP-B` needs `bt 6.5` by time `6.5` so it has `.5` to produce `bt 7`.

Please notice that the time of `bt 7` minus `.5` equals the time of `bt 6.5` therefore time `t` is the last block time of `BP-A` and when `BP-B` needs to start its first block.

A block is produced and sent when either it reaches `m` or `u` or `p`. 

Starting in Leap 4.0, blocks are propagated after block header validation. This means instead of `BP-A Peer` & `BP-B Peer` taking `m` time to validate and forward a block it only takes a small number of milliseconds to verify the block header and then forward the block.

Starting in Leap 5.0, blocks in a round are started immediately after the completion of the previous block. Before 5.0, blocks were always started on `w` intervals and a node would "sleep" between blocks if needed. In 5.0, the "sleeps" are all moved to the end of the block production round. 

## Example 1: block arrives 110ms early
* Assuming zero network latency between BP and immediate BP peer.
* Assuming blocks do not reach `m` and therefore take `w - a` time to produce.
* Assume block completion including signing takes zero time.
* `BP-A` has e = 120, n = 0ms, h = 5ms, a = 10ms
* `BP-A` sends b1 at `t1-10ms` => `BP-A-Peer` processes `h=5ms`, sends at `t-5ms` => `BP-B-Peer` processes `h=5ms`, sends at `t-0ms` => arrives at `BP-B` at `t`.
* `BP-A` starts b2 at `t1-10ms`, sends b2 at `t2-20ms` => `BP-A-Peer` processes `h=5ms`, sends at `t2-15ms` => `BP-B-Peer` processes `h=5ms`, sends at `t2-10ms` => arrives at `BP-B` at `t2-10ms`.
* `BP-A` starts b3 at `t2-20ms`, ...
* `BP-A` starts b12 at `t11-110ms`, sends b12 at `t12-120ms` => `BP-A-Peer` processes `h=5ms`, sends at `t12-115ms` => `BP-B-Peer` processes `h=5ms`, sends at `t12-110ms` => arrives at `BP-B` at `t12-110ms`

## Example 2: block arrives 80ms early
* Assuming zero network latency between BP and immediate BP peer.
* Assuming blocks do not reach `m` and therefore take `w - a` time to produce.
* Assume block completion including signing takes zero time.
* `BP-A` has e = 240, n = 150ms, h = 5ms, a = 20ms
* `BP-A` sends b1 at `t1-20ms` => `BP-A-Peer` processes `h=5ms`, sends at `t-15ms` =(150ms)> `BP-B-Peer` processes `h=5ms`, sends at `t+140ms` => arrives at `BP-B` at `t+140ms`.
* `BP-A` starts b2 at `t1-20ms`, sends b2 at `t2-40ms` => `BP-A-Peer` processes `h=5ms`, sends at `t2-35ms` =(150ms)> `BP-B-Peer` processes `h=5ms`, sends at `t2+120ms` => arrives at `BP-B` at `t2+120ms`.
* `BP-A` starts b3 at `t2-40ms`, ...
* `BP-A` starts b12 at `t11-220ms`, sends b12 at `t12-240ms` => `BP-A-Peer` processes `h=5ms`, sends at `t12-235ms` =(150ms)> `BP-B-Peer` processes `h=5ms`, sends at `t12-80ms` => arrives at `BP-B` at `t12-80ms`

## Example 3: block arrives 16ms late and is dropped
* Assuming zero network latency between BP and immediate BP peer.
* Assuming blocks do not reach `m` and therefore take `w - a` time to produce.
* Assume block completion including signing takes zero time.
* `BP-A` has e = 204, n = 200ms, h = 10ms, a = 17ms
* `BP-A` sends b1 at `t1-17ms` => `BP-A-Peer` processes `h=10ms`, sends at `t-7ms` =(200ms)> `BP-B-Peer` processes `h=10ms`, sends at `t+203ms` => arrives at `BP-B` at `t+203ms`.
* `BP-A` starts b2 at `t1-17ms`, sends b2 at `t2-34ms` => `BP-A-Peer` processes `h=10ms`, sends at `t2-24ms` =(200ms)> `BP-B-Peer` processes `h=10ms`, sends at `t2+186ms` => arrives at `BP-B` at `t2+186ms`.
* `BP-A` starts b3 at `t2-34ms`, ...
* `BP-A` starts b12 at `t11-187ms`, sends b12 at `t12-204ms` => `BP-A-Peer` processes `h=10ms`, sends at `t12-194ms` =(200ms)> `BP-B-Peer` processes `h=10ms`, sends at `t12+16ms` => arrives at `BP-B` at `t12-16ms`

## Example 4: full blocks are produced early
* Assuming zero network latency between BP and immediate BP peer.
* Assume all blocks are full as there are enough queued up unapplied transactions ready to fill all blocks.
* Assume a block can be produced with 200ms worth of transactions in 225ms worth of time. There is overhead for producing the block.
* `BP-A` has e = 120, m = 200ms, n = 200ms, h = 10ms, a = 10ms
* `BP-A` sends b1 at `t1-275s` => `BP-A-Peer` processes `h=10ms`, sends at `t-265ms` =(200ms)> `BP-B-Peer` processes `h=10ms`, sends at `t-55ms` => arrives at `BP-B` at `t-55ms`.
* `BP-A` starts b2 at `t1-275ms`, sends b2 at `t2-550ms (t1-50ms)` => `BP-A-Peer` processes `h=10ms`, sends at `t2-540ms` =(200ms)> `BP-B-Peer` processes `h=10ms`, sends at `t2-330ms` => arrives at `BP-B` at `t2-330ms`.
* `BP-A` starts b3 at `t2-550ms`, ...
* `BP-A` starts b12 at `t11-3025ms`, sends b12 at `t12-3300ms` => `BP-A-Peer` processes `h=10ms`, sends at `t12-3290ms` =(200ms)> `BP-B-Peer` processes `h=10ms`, sends at `t12-3080ms` => arrives at `BP-B` at `t12-3080ms`


Running wasm-runtime=eos-vm-jit eos-vm-oc-enable on relay node will reduce the validation time.
