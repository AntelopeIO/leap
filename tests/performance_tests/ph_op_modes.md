# Performance Harness Operational Modes

## Block Producer Mode

### Topology

Standard default topology includes:
- 1 Producer Node
- 1 Validation Node
- Transaction Generator(s) directing transaction traffic to Producer Node's p2p listen endpoint

### Use Case

Determine the maximum throughput (transactions per second) of a specified transaction type (token transfer, new account, etc.) the single producer system can sustain.
The validation node is used by the performance harness framework to monitor and query information about the performance and progress of the producer node, so as to not
adversely affect the producer's ability to process transactions by answering queries from the performance harness concurrently.

### Configuration

The perfomance_test.py and performance_test_basic.py are currently configured to default to the Block Producer Mode of Operation.

### Performance Measurements

- Maximum Transactions Per Second (TPS) achieved/sustained
- Block Size (min, max, avg, std deviation, empty blocks, total blocks)
- Transactions Per Second (min, max, avg, std deviation, empty blocks, total blocks)
- Transaction CPU usage/performance (min, max, avg, std deviation, number of samples)
- Transaction Net usage/performance (min, max, avg, std deviation, number of samples)
- Transaction Latency - Measured from time sent to block inclusion time (min, max, avg, std deviation, num samples)
- Dropped Blocks
- Dropped Transactions
- Production Windows (total, avg size, missed)  
- Forked blocks
- Fork Count
