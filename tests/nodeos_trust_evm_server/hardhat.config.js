require("@nomicfoundation/hardhat-toolbox");
require("@nomiclabs/hardhat-web3");

// task action function receives the Hardhat Runtime Environment as second argument
task("accounts", "Prints accounts", async (_, { web3 }) => {
  console.log(await web3.eth.getAccounts());
});

task("blockNumber", "Prints the current block number", async (_, { web3 }) => {
  console.log(await web3.eth.getBlockNumber());
});

task("block", "Prints block")
  .addParam("blocknum", "The block number")
  .setAction(async (taskArgs) => {
    const block = await ethers.provider.getBlockWithTransactions(taskArgs.blocknum);
    console.log(block);
});

task("balance", "Prints an account's balance")
  .addParam("account", "The account's address")
  .setAction(async (taskArgs) => {
    const balance = await ethers.provider.getBalance(taskArgs.account);
    console.log(ethers.utils.formatEther(balance), "ETH");
});

task("nonce", "Prints an account's nonce")
  .addParam("account", "The account's address")
  .setAction(async (taskArgs) => {
    const nonce = await ethers.provider.getTransactionCount(taskArgs.account);
    console.log(nonce);
});

task("transfer", "Send ERC20 tokens")
  .addParam("from", "from account")
  .addParam("to", "to account")
  .addParam("contract", "ERC20 token address")
  .addParam("amount", "amount to trasfer")
  .setAction(async (taskArgs) => {
    const Token = await ethers.getContractFactory('Token')
    const token = Token.attach(taskArgs.contract)
    const res = await token.connect(await ethers.getSigner(taskArgs.from)).transfer(taskArgs.to, ethers.utils.parseEther(taskArgs.amount.toString()),{gasLimit:50000});
    console.log(res);
});

task("send-loop", "Send ERC20 token in a loop")
  .addParam("contract", "Token contract address")
  .setAction(async (taskArgs) => {
    const accounts = await web3.eth.getAccounts();
    const Token = await ethers.getContractFactory('Token')
    const token = Token.attach(taskArgs.contract)
    while(true) {
      const destination = accounts[parseInt(Math.random()*accounts.length)];
      const amount = ethers.utils.parseEther((1+Math.random()*3).toString());
      const res = await token.transfer(destination, amount)
      console.log(res);
      console.log("############################################ SENT #######");
    }
});

task("storage-loop", "Store incremental values to the storage contract")
  .addParam("contract", "Token contract address")
  .setAction(async (taskArgs) => {
    const Storage = await ethers.getContractFactory('Storage')
    const storage = Storage.attach(taskArgs.contract)
    let value = 1;
    while(true) {
      const res = await storage.store(value, {gasLimit:50000})
      console.log("############################################ "+ value +" STORED #######");
      ++value;
    }
});


/** @type import('hardhat/config').HardhatUserConfig */
module.exports = {
  defaultNetwork: "ltrust",
  networks: {
    ltrust: {
      url: "http://localhost:5000",
      accounts: {
        mnemonic: "test test test test test test test test test test test junk",
        path: "m/44'/60'/0'/0",
        initialIndex: 0,
        count: 20,
        passphrase: "",
      },
    }
  },
  solidity: "0.8.17",
};
