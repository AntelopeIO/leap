#include <eosio/eosio.hpp>
#include <eosio/crypto.hpp>
#include <eosio/crypto_bls_ext.hpp>
#include <eosio/system.hpp>
#include <eosio/transaction.hpp>

#include "bitset.hpp"

#include <numeric>
#include <queue>

using namespace eosio;

CONTRACT svnn_ibc : public contract {
   public:
      using contract::contract;

      using bls_public_key = std::vector<char>;
      using bls_signature = std::vector<char>;

      const uint32_t POLICY_CACHE_EXPIRY = 600; //10 minutes for testing
      const uint32_t PROOF_CACHE_EXPIRY = 600; //10 minutes for testing

      //Compute the maximum number of layers of a merkle tree for a given number of leaves
      uint64_t calculate_max_depth(uint64_t node_count) {
         if(node_count <= 1)
            return node_count;
         return 64 - __builtin_clzll(2 << (64 - 1 - __builtin_clzll ((node_count - 1))));
      }
      
      static uint32_t reverse_bytes(const uint32_t input){
         uint32_t output = (input>>24 & 0xff)|(input>>8 & 0xff00)|(input<<8 & 0xff0000)|(input<<24 & 0xff000000);
         return output;
      }

      static checksum256 hash_pair(const std::pair<checksum256, checksum256> p){
         std::array<uint8_t, 32> arr1 = p.first.extract_as_byte_array();
         std::array<uint8_t, 32> arr2 = p.second.extract_as_byte_array();
         std::array<uint8_t, 64> result;
         std::copy (arr1.cbegin(), arr1.cend(), result.begin());
         std::copy (arr2.cbegin(), arr2.cend(), result.begin() + 32);
         checksum256 hash = sha256(reinterpret_cast<char*>(result.data()), 64);
         return hash;
      }

      static time_point add_time(const time_point& time, const uint32_t seconds ){
         int64_t total_seconds = (static_cast<int64_t>(time.sec_since_epoch()) + static_cast<int64_t>(seconds));
         microseconds ms = microseconds(total_seconds * 1000000);
         time_point tp = time_point(ms);
         return tp;
      }

      /*

      //discuss : compute merkle branch direction vs providing them as part of the proof

      static std::vector<uint8_t> _get_directions(const uint64_t index, const uint64_t last_node_index){

          std::vector<uint8_t> proof;

          uint64_t c_index = index;
          uint64_t layers_depth = calculate_max_depth(last_node_index) -1;
          uint64_t c_last_node_index = last_node_index;

          for (uint64_t i = 0; i < layers_depth; i++) {
              if (c_last_node_index % 2) c_last_node_index+=1;
              bool isLeft = c_index % 2 == 0 ? 0 : 1;
              uint64_t pairIndex = isLeft ? c_index - 1 :
                          c_index == last_node_index - 1 && i < layers_depth - 1 ? c_index :
                          c_index + 1;
              c_last_node_index/=2;
              if (pairIndex < last_node_index) proof.push_back(isLeft);
              c_index = c_index / 2;
          }
          return proof;
      }

      */

      struct merkle_branch {
         uint8_t direction;
         checksum256 hash;
      };


      //compute the merkle root of target node and vector of merkle branches
      static checksum256 _compute_root(const std::vector<merkle_branch> proof_nodes, const checksum256& target){
          checksum256 hash = target;
          for (int i = 0 ; i < proof_nodes.size() ; i++){
              const checksum256 node = proof_nodes[i].hash;
              std::array<uint8_t, 32> arr = node.extract_as_byte_array();
              if (proof_nodes[i].direction == 0){
                  hash = hash_pair(std::make_pair(hash, node));
              }
              else {
                  hash = hash_pair(std::make_pair(node, hash));
              }
          }
          return hash;
      }

      struct quorum_certificate {
          std::vector<uint64_t> finalizers;
          bls_signature signature;
      };

      struct finalizer_authority {
         std::string     description;
         uint64_t        fweight = 0;
         bls_public_key  public_key;
      };

      struct fpolicy {

         uint32_t                         generation = 0; ///< sequentially incrementing version number
         uint64_t                         fthreshold = 0;  ///< vote fweight threshold to finalize blocks
         std::vector<finalizer_authority> finalizers; ///< Instant Finality voter set

         checksum256 digest() const {
             std::vector<char> serialized = pack(*this);
             return sha256(serialized.data(), serialized.size());
         }

      };

      //finalizer policy augmented with contextually-relevant data 
      TABLE storedpolicy : fpolicy {

         uint32_t       last_block_num = 0; //last block number where this policy is in force

         time_point     cache_expiry; //cache expiry

         uint64_t primary_key() const {return generation;}
         uint64_t by_cache_expiry()const { return cache_expiry.sec_since_epoch(); }

         EOSLIB_SERIALIZE( storedpolicy, (generation)(fthreshold)(finalizers)(last_block_num)(cache_expiry))

      };

      TABLE lastproof {

          uint64_t         id;

          uint32_t         block_num;
          
          checksum256      finality_mroot;

          time_point       cache_expiry;

          uint64_t primary_key()const { return id; }
          uint64_t by_block_num()const { return block_num; }
          uint64_t by_cache_expiry()const { return cache_expiry.sec_since_epoch(); }
          checksum256 by_merkle_root()const { return finality_mroot; }

      };

      struct authseq {
         name account;
         uint64_t sequence;

         EOSLIB_SERIALIZE( authseq, (account)(sequence) )

      };

      struct r_action_base {
         name             account;
         name             name;
         std::vector<permission_level> authorization;

      };

      struct r_action :  r_action_base {
         std::vector<char>    data;
         std::vector<char>    returnvalue;

         checksum256 digest() const {
            checksum256 hashes[2];
            const r_action_base* base = this;
            const auto action_input_size = pack_size(data);
            const auto return_value_size = pack_size(returnvalue);
            const auto rhs_size = action_input_size + return_value_size;
            const auto serialized_base = pack(*base);
            const auto serialized_data = pack(data);
            const auto serialized_output = pack(returnvalue);
            hashes[0] = sha256(serialized_base.data(), serialized_base.size());
            std::vector<uint8_t> data_digest(action_input_size);
            std::vector<uint8_t> output_digest(return_value_size);
            std::vector<uint8_t> h1_result(rhs_size);
            std::copy (serialized_data.cbegin(), serialized_data.cend(), h1_result.begin());
            std::copy (serialized_output.cbegin(), serialized_output.cend(), h1_result.begin() + action_input_size);
            hashes[1] = sha256(reinterpret_cast<char*>(h1_result.data()), rhs_size);
            std::array<uint8_t, 32> arr1 = hashes[0].extract_as_byte_array();
            std::array<uint8_t, 32> arr2 = hashes[1].extract_as_byte_array();
            std::array<uint8_t, 64> result;
            std::copy (arr1.cbegin(), arr1.cend(), result.begin());
            std::copy (arr2.cbegin(), arr2.cend(), result.begin() + 32);
            checksum256 final_hash = sha256(reinterpret_cast<char*>(result.data()), 64);
            return final_hash;
         }

         EOSLIB_SERIALIZE( r_action, (account)(name)(authorization)(data)(returnvalue))

      };

      struct action_receipt {

         name                       receiver;

         //act_digest is provided instead by obtaining the action digest. Implementation depends on the activation of action_return_value feature
         //checksum256              act_digest;

         uint64_t                   global_sequence = 0;
         uint64_t                   recv_sequence   = 0;
   
         std::vector<authseq>       auth_sequence;
         unsigned_int               code_sequence = 0;
         unsigned_int               abi_sequence  = 0;

         EOSLIB_SERIALIZE( action_receipt, (receiver)(global_sequence)(recv_sequence)(auth_sequence)(code_sequence)(abi_sequence) )

      };

      struct proof_of_inclusion;

      struct dynamic_data_v0 {

         //block_num is always present
         uint32_t block_num;

         //can include any number of action_proofs and / or state_proofs pertaining to a given block
         //all action_proofs must resolve to the same action_mroot
         std::vector<proof_of_inclusion> action_proofs;

         //can be used instead of providing action_proofs. Useful for proving finalizer policy changes
         std::optional<checksum256> action_mroot;

         checksum256 get_action_mroot() const {
            if (action_mroot.has_value()) return action_mroot.value();
            else {
               check(action_proofs.size()>0, "must have at least one action proof");
               checksum256 root = checksum256();
               for (auto ap : action_proofs){
                  if (root == checksum256()) root = ap.root();
                  else check(ap.root() == root, "all action proofs must resolve to the same merkle root");
               }
               return root;
            }
         }; 
      };
            
      struct block_finality_data {
         
         //major_version for this block
         uint32_t major_version;

         //minor_version for this block
         uint32_t minor_version;

         //finalizer_policy_generation for this block
         uint32_t finalizer_policy_generation;

         //if the block to prove contains a finalizer policy change, it can be provided
         std::optional<fpolicy> active_finalizer_policy;

         //if a finalizer policy is present, witness_hash should be the base_digest. Otherwise, witness_hash should be the static_data_digest
         checksum256 witness_hash;

         //final_on_qc for this block
         checksum256 finality_mroot;

         //returns hash of digest of active_finalizer_policy + witness_hash if active_finalizer_policy is present, otherwise returns witness_hash
         checksum256 resolve_witness() const {
            if (active_finalizer_policy.has_value()){
               std::vector<char> serialized_policy = pack(active_finalizer_policy.value());
               checksum256 policy_digest = sha256(serialized_policy.data(), serialized_policy.size());
               checksum256 base_fpolicy_digest = hash_pair( std::make_pair( policy_digest, witness_hash) );
               return base_fpolicy_digest;
            }
            else {
               return witness_hash;
            }
         }; 

         //returns hash of major_version + minor_version + finalizer_policy_generation + resolve_witness() + finality_mroot
         checksum256 finality_digest() const {
            std::array<uint8_t, 76> result;
            memcpy(&result[0], (uint8_t *)&major_version, 4);
            memcpy(&result[4], (uint8_t *)&minor_version, 4);
            memcpy(&result[8], (uint8_t *)&finalizer_policy_generation, 4);
            std::array<uint8_t, 32> arr1 = finality_mroot.extract_as_byte_array();
            std::array<uint8_t, 32> arr2 = resolve_witness().extract_as_byte_array();
            std::copy (arr1.cbegin(), arr1.cend(), result.begin() + 12);
            std::copy (arr2.cbegin(), arr2.cend(), result.begin() + 44);
            checksum256 hash = sha256(reinterpret_cast<char*>(result.data()), 76);
            return hash;
         };

      };

      struct block_data {
         
         //finality data
         block_finality_data finality_data;

         //dynamic_data to be verified
         dynamic_data_v0 dynamic_data;

         //returns hash of finality_digest() and dynamic_data_digest()
         checksum256 digest() const {
            checksum256 finality_digest = finality_data.finality_digest();
            checksum256 action_mroot = dynamic_data.get_action_mroot();
            std::array<uint8_t, 76> result;
            memcpy(&result[0], (uint8_t *)&finality_data.major_version, 4);
            memcpy(&result[4], (uint8_t *)&finality_data.minor_version, 4);
            memcpy(&result[8], (uint8_t *)&dynamic_data.block_num, 4);
            std::array<uint8_t, 32> arr1 = finality_digest.extract_as_byte_array();
            std::array<uint8_t, 32> arr2 = action_mroot.extract_as_byte_array();
            std::copy (arr1.cbegin(), arr1.cend(), result.begin() + 12);
            std::copy (arr2.cbegin(), arr2.cend(), result.begin() + 44);
            checksum256 hash = sha256(reinterpret_cast<char*>(result.data()), 76);
            return hash;
         };
      };

      struct action_data {

         r_action action; //antelope action
         checksum256 action_receipt_digest; //required witness hash, actual action_receipt is irrelevant to IBC

         std::vector<char> return_value; //empty if no return value

         //returns the action digest 
         checksum256 action_digest() const {
            return action.digest();
         }; 

         //returns the receipt digest, composed of the action_digest() and action_receipt_digest witness hash
         checksum256 digest() const {
            checksum256 action_receipt_digest = hash_pair( std::make_pair( action_digest(), action_receipt_digest) );
            return action_receipt_digest;
         };

      };

      using target_data = std::variant<block_data, action_data>;


      struct proof_of_inclusion {

         uint64_t target_node_index;
         uint64_t last_node_index;

         target_data target;

         std::vector<merkle_branch> merkle_branches;

         //returns the merkle root obtained by hashing target.digest() with merkle_branches
         checksum256 root() const {
            auto call_digest = [](const auto& var) -> checksum256 { return var.digest(); };
            checksum256 digest = std::visit(call_digest, target);
            checksum256 root = _compute_root(merkle_branches, digest);
            return root;
         }; 

      };

      struct finality_proof {

         //block finality data over which we validate a QC
         block_finality_data qc_block;

         //signature over finality_digest() of qc_block. 
         quorum_certificate qc;

      };

      struct proof {

         //valid configurations :
         //1) finality_proof for a QC block, and proof_of_inclusion of a target block within the final_on_strong_qc block represented by the finality_mroot present in header
         //2) only a proof_of_inclusion of a target block, which must be included in a merkle tree represented by a root stored in the contract's RAM
         std::optional<finality_proof> finality_proof;
         proof_of_inclusion target_block_proof_of_inclusion;

      };

      typedef eosio::multi_index< "policies"_n, storedpolicy,
          indexed_by<"expiry"_n, const_mem_fun<storedpolicy, uint64_t, &storedpolicy::by_cache_expiry>>> policies_table;

      typedef eosio::multi_index< "lastproofs"_n, lastproof,
          indexed_by<"blocknum"_n, const_mem_fun<lastproof, uint64_t, &lastproof::by_block_num>>,
          indexed_by<"merkleroot"_n, const_mem_fun<lastproof, checksum256, &lastproof::by_merkle_root>>,
          indexed_by<"expiry"_n, const_mem_fun<lastproof, uint64_t, &lastproof::by_cache_expiry>>> proofs_table;

      std::vector<char> _g1add(const std::vector<char>& op1, const std::vector<char>& op2);

      void _maybe_set_finalizer_policy(const fpolicy& policy, const uint32_t from_block_num);
      void _maybe_add_proven_root(const uint32_t block_num, const checksum256& finality_mroot);

      void _garbage_collection();

      void _verify(const std::vector<char>& pk, const std::vector<char>& sig, std::vector<const char>& msg);
      void _check_qc(const quorum_certificate& qc, const checksum256& finality_mroot, const uint64_t finalizer_policy_generation);
      
      void _check_finality_proof(const finality_proof& finality_proof, const proof_of_inclusion& target_block_proof_of_inclusion);
      void _check_target_block_proof_of_inclusion(const proof_of_inclusion& proof, const std::optional<checksum256> reference_root);

      ACTION setfpolicy(const fpolicy& policy, const uint32_t from_block_num); //set finality policy
      ACTION checkproof(const proof& proof);

      //clearing function, to be removed for production version
      ACTION clear();

};