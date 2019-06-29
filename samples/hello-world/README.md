# Hello World sample

## The idea of this sample is to show how to build and deploy a simple smart contract project.

### Requirements
- docker, installed and configured to run without sudo
- make
- Your WAX account and its private key (to deploy the contract)

### Build and deploy

For building and deploying the contract you are going to use the *make* utility. The syntax is:

```
$ make [ build |
         clean |
         create-key |
         create-account CREATOR=<creator name> NAME=<account name> PUBLIC_KEY=<account public key> |
         deploy CONTRACT_ACCOUNT=<account used to deploy> 
                CONTRACT_PRIVATE_KEY=<the account private key> [NODEOS_URL=<deployment URL>] ]
```

#### Notes:
- The building process uses our [development image](https://hub.docker.com/r/waxteam/dev) from docker hub.
- Be aware that you need to build your contract first in order to deploy it.
- CONTRACT_ACCOUNT parameter is mandatory to deploy the contract.
- CONTRACT_PRIVATE_KEY parameter is mandatory to deploy the contract. It's the associated private key of the contract account.
- NODEOS_URL parameter is optional, its default value is https://chain.wax.io/

#### Example:
```
# Download the code
git clone https://github.com/worldwide-asset-exchange/wax-blockchain.git
cd wax-blockchain/samples/hello-world

# Build the smart contract
make build

# Optional (unless you have one): create a pair of private/public keys (save the results in 
# a safe place, they are going to be printed on the screen)
make create-key

# Optional (unless you have one): create an account
make create-account CREATOR=foocreator NAME=foo PUBLIC_KEY=<public key from 'create-key'>

# Deploy the smart contract to the mainnet
make deploy CONTRACT_ACCOUNT=foo CONTRACT_PRIVATE_KEY=<private key from 'create-key'>
```
