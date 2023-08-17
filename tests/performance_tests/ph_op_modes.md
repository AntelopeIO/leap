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

## API Node (HTTP Node) Mode

### Topology

Topology includes:
- 1 Producer Node
- 1 API Node (http_plugin enabled)
- 1 Validation Node
- Transaction Generator(s) directing http transaction traffic to API Node's http listen endpoint

### Use Case

Determine the maximum throughput (transactions per second) of a specified transaction type (token transfer, read only, new account, etc.) the single producer system can sustain.
The validation node is used by the performance harness framework to monitor and query information about the performance and progress of the producer node as it processes API node transactions,
so as to not adversely affect the producer's ability to process transactions by answering queries from the performance harness concurrently.

### Configuration

Additional node included over the base configuration. This additional node will have the http_plugin enabled and will be the recipient of all generated transaction traffic.
Configure unlimited:
- `chain_plugin`
    - `max_bytes_in_flight`
    - `max_requests_in_flight`
    - `http_max_response_time`

### Performance Measurements

- How many transactions can be pushed/sent to a single API node
- How many read-only transactions can be processed by a single API node with defined number of threads (cur. limit max 8)
- How many reads are possible (i.e. get table calls) by a single API node
- Transaction Latency - Measured from time sent to block inclusion time (min, max, avg, std deviation, num samples)
- HTTP response time statistics
