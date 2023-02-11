# EOS Mechanics
This is a collection of EOS contracts and utilities in use by the [EOS Mechanics](https://eosmechanics.com/) research group.

Please visit us on [Telegram](https://t.me/EOSMechanics) for any feedback or questions. 

## Benchmarks
The benchmarks below are EOS contracts which are set on the `eosmechanics` account on Mainnet, CryptoKylin Testnet, and Jungle Testnet. They are executed during each block producers' schedule, and the timings recorded on-chain using the standard `cpu_usage_us` transaction field. The data is [freely available](https://eosflare.io/account/eosmechanics) to view and analyze, and we encourage doing so to help identify issues and improve block producer performance.

Example tools that utilize this data:

- [EOS Block Producer Benchmarks](https://www.alohaeos.com/tools/benchmarks) by [Aloha EOS](https://www.alohaeos.com/)
- [Block Producer Performance](https://labs.eostitan.com/#/block-producer-performance) by [EOS Titan](https://eostitan.com/)



### CPU Benchmark

This benchmark targets the CPU by calculating Mersenne prime numbers. Calculating primes is an industry standard for measuring CPU performance and it uses code operations that are common in software development.

### RAM Benchmark

This benchmark targets EOS RAM by rapidly writing to and reading from a RAM table. Due to inefficiencies within the EOSIO software this benchmark is currently CPU heavy and thus we consider it experimental and very similar to the CPU benchmark. As the software performance is improved we expect the results of this benchmark to become more meaningful.

--

#### Disclaimer
*EOS Token holders should not rely on EOS Mechanics benchmark results as an absolute indication of block producers’ performance. Results are mainly intended for operators of the EOS infrastructure to have additional metrics that can aid them in testing different infrastructures, configurations and features while identifying performance differences and potential bottlenecks. These statistics are not subject to verification by other nodes on the network; therefore it is possible for block producers to manipulate them. Furthermore, running custom software or configurations may impact the measurement of these metrics.*

