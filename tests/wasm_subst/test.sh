set -e

nodeos \
    -e \
    -p eosio \
    --plugin eosio::producer_plugin \
    --plugin eosio::producer_api_plugin \
    --plugin eosio::chain_plugin \
    --plugin eosio::chain_api_plugin \
    --plugin eosio::http_plugin \
    --plugin eosio::state_history_plugin \
    --plugin eosio::subst_plugin \
    --disable-replay-opts \
    --http-server-address 127.0.0.1:8888 \
    --p2p-listen-endpoint 127.0.0.1:9010 \
    --access-control-allow-origin=* \
    --contracts-console \
    --http-validate-host=false \
    --verbose-http-errors \
    --subst helloworld.wasm:debugworld.wasm \
    --data-dir=data \
    >> "nodeos.log" 2>&1 &

sleep 5

cleos wallet create --file wallet_pass

cleos wallet open

WALLET_PASS="$(cat wallet_pass)"

cleos wallet unlock --password "$WALLET_PASS"

# eosio dev key
cleos wallet import --private-key 5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3

cleos wallet import --private-key 5KNmBfzuTD3L4DBAQvY3wjipJSpicKhpobAdSK7dAzbsw57X6zX

cleos create account eosio helloworld EOS7Sp9Z1ahCNpVGGsSPRJCtYDTg8YdqEzt63s7y8mXgyfw4zVndo

cleos set contract helloworld . helloworld.wasm helloworld.abi -p helloworld@active 

cleos push action helloworld hi "[]" -p eosio@active
