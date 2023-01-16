#include <eosio/eosio.hpp>
#include <eosio/print.hpp>

class [[eosio::contract]] debugworld : public eosio::contract {
    public:
        using eosio::contract::contract;

        [[eosio::action]]
        void hi();
};
