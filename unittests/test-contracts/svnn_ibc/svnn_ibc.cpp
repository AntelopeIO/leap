#include "svnn_ibc.hpp"

//add two numbers from the g1 group (aggregation)
std::vector<char> svnn_ibc::_g1add(const std::vector<char>& op1, const std::vector<char>& op2) {
   check(op1.size() == std::tuple_size<bls_g1>::value, "wrong op1 size passed");
   check(op2.size() == std::tuple_size<bls_g1>::value, "wrong op2 size passed");
   bls_g1 r;
   bls_g1_add(*reinterpret_cast<const bls_g1*>(op1.data()), *reinterpret_cast<const bls_g1*>(op2.data()), r);
   std::vector<char> v(r.begin(), r.end());
   return v;
}

void svnn_ibc::_maybe_set_finalizer_policy(const fpolicy& policy, const uint32_t from_block_num){
    policies_table _policies_table(get_self(), get_self().value);
    auto itr = _policies_table.rbegin();
    //if the new policy is more recent than the most recent we are aware of, we record the new one
    if (itr==_policies_table.rend() || itr->generation < policy.generation){

        //if a previous policy was in force, it is now superseded by the newer one for any future proof verification
        if (itr!=_policies_table.rend()){
            auto fwd_itr = itr.base();
            fwd_itr--;
            _policies_table.modify( fwd_itr, same_payer, [&]( auto& sfp ) {
                sfp.last_block_num = from_block_num;
           });
        }
        svnn_ibc::storedpolicy spolicy;
        spolicy.generation = policy.generation;
        spolicy.fthreshold = policy.fthreshold;
        spolicy.finalizers = policy.finalizers;

        //policy is in force until a newer policy is proven
        spolicy.last_block_num = std::numeric_limits<uint32_t>::max();
        //set cache expiry
        spolicy.cache_expiry = add_time(current_time_point(), POLICY_CACHE_EXPIRY);
        _policies_table.emplace( get_self(), [&]( auto& p ) {
            p = spolicy;
        });
    }
}

//adds the newly proven root if necessary
void svnn_ibc::_maybe_add_proven_root(const uint32_t block_num, const checksum256& finality_mroot){
    proofs_table _proofs_table(get_self(), get_self().value);
    auto block_num_index = _proofs_table.get_index<"blocknum"_n>();
    auto merkle_index = _proofs_table.get_index<"merkleroot"_n>();
    auto last_itr = block_num_index.rbegin();

    //if first proven root or newer than the last proven root, we store it 
    if (last_itr == block_num_index.rend() || last_itr->block_num<block_num){
        auto itr = merkle_index.find(finality_mroot);
        if (itr == merkle_index.end()){
            _proofs_table.emplace( get_self(), [&]( auto& p ) {
                p.id = _proofs_table.available_primary_key();
                p.block_num = block_num;
                p.finality_mroot = finality_mroot;
                p.cache_expiry = add_time(current_time_point(), PROOF_CACHE_EXPIRY); //set cache expiry
            });
        }
    }
    //otherwise, the proven root is not advancing finality so we don't need to store it
}

//delete old policies and proofs that are no longer necessary
void svnn_ibc::_garbage_collection(){
    //todo : implement
}

//verify that a signature over a given message has been generated with the private key matching the public key
void svnn_ibc::_verify(const std::vector<char>& pk, const std::vector<char>& sig, std::vector<const char>& msg){
    check(pk.size() == std::tuple_size<bls_g1>::value, "wrong pk size passed");
    check(sig.size() == std::tuple_size<bls_g2>::value, "wrong sig size passed");
    bls_g1 g1_points[2];
    bls_g2 g2_points[2];

    memcpy(g1_points[0].data(), eosio::detail::G1_ONE_NEG.data(), sizeof(bls_g1));
    memcpy(g2_points[0].data(), sig.data(), sizeof(bls_g2));

    bls_g2 p_msg;
    eosio::detail::g2_fromMessage(msg, eosio::detail::CIPHERSUITE_ID, p_msg);
    memcpy(g1_points[1].data(), pk.data(), sizeof(bls_g1));
    memcpy(g2_points[1].data(), p_msg.data(), sizeof(bls_g2));

    bls_gt r;
    bls_pairing(g1_points, g2_points, 2, r);
    check(0 == memcmp(r.data(), eosio::detail::GT_ONE.data(), sizeof(bls_gt)), "bls signature verify failed");
}

//verify that the quorum certificate over the finality digest is valid
void svnn_ibc::_check_qc(const quorum_certificate& qc, const checksum256& finality_digest, const uint64_t finalizer_policy_generation){

    policies_table _policies_table(get_self(), get_self().value);
    check(_policies_table.begin() != _policies_table.end(), "must set a finalizer policy before checking proofs");

    //fetch the finalizer policy where generation num is equal prepare.input.finalizer_policy_generation, and that it is still in force
    auto itr = _policies_table.find(finalizer_policy_generation);
    check(itr!=_policies_table.end(), "finalizer policy not found");
    storedpolicy target_policy = *itr;
    auto fa_itr = target_policy.finalizers.begin();
    auto fa_end_itr = target_policy.finalizers.end();
    size_t finalizer_count = std::distance(fa_itr, fa_end_itr);
    std::vector<uint64_t> bitset_data(qc.finalizers);
    bitset b(finalizer_count, bitset_data);

    bool first = true;

    size_t index = 0;
    uint64_t weight = 0;

    bls_public_key agg_pub_key;

    while (fa_itr != fa_end_itr){
        if (b.test(index)){
            if (first){
                first=false;
                agg_pub_key = fa_itr->public_key;
            }
            else agg_pub_key = _g1add(agg_pub_key, fa_itr->public_key);
            weight+=fa_itr->fweight;

        }
        index++;
        fa_itr++;
    }

    //verify that we have enough vote weight to meet the quorum threshold of the target policy
    check(weight>=target_policy.fthreshold, "insufficient signatures to reach quorum");
    std::array<uint8_t, 32> data = finality_digest.extract_as_byte_array();
    std::vector<const char> v_data(data.begin(), data.end());
    //verify signature validity
    _verify(agg_pub_key, qc.signature, v_data);
}

void svnn_ibc::_check_target_block_proof_of_inclusion(const proof_of_inclusion& proof, const std::optional<checksum256> reference_root){

    //verify that the proof of inclusion is over a target block
    check(std::holds_alternative<svnn_ibc::block_data>(proof.target), "must supply proof of inclusion over block data");

    //resolve the proof to its merkle root
    checksum256 finality_mroot = proof.root();
    if (reference_root.has_value()){
        check(reference_root.value() == finality_mroot, "cannot link proof to proven merkle root");
    }
    else {
        proofs_table _proofs_table(get_self(), get_self().value);
        auto merkle_index = _proofs_table.get_index<"merkleroot"_n>();
        auto itr = merkle_index.find(finality_mroot);
        check(itr!= merkle_index.end(), "cannot link proof to proven merkle root");
    }
    block_data target_block = std::get<svnn_ibc::block_data>(proof.target);
    if (target_block.finality_data.active_finalizer_policy.has_value()){
        _maybe_set_finalizer_policy(target_block.finality_data.active_finalizer_policy.value(), target_block.dynamic_data.block_num);
    }
}

void svnn_ibc::_check_finality_proof(const finality_proof& finality_proof, const proof_of_inclusion& target_block_proof_of_inclusion){
    //temporarilly disabled : skip qc verification
    //if QC is valid, it means that we have reaced finality on the block referenced by the finality_mroot
    //_check_qc(finality_proof.qc, finality_proof.qc_block.finality_digest(), finality_proof.qc_block.finalizer_policy_generation);

    //check if the target proof of inclusion correctly resolves to the root of the finality proof
    _check_target_block_proof_of_inclusion(target_block_proof_of_inclusion, finality_proof.qc_block.finality_mroot);
    
    //if proof of inclusion was successful, the target block and its dynamic data have been validated as final and correct
    block_data target_block = std::get<svnn_ibc::block_data>(target_block_proof_of_inclusion.target);

    //if the finality_mroot we just proven is more recent than the last root we have stored, store it
    _maybe_add_proven_root(target_block.dynamic_data.block_num, finality_proof.qc_block.finality_mroot);
}

ACTION svnn_ibc::setfpolicy(const fpolicy& policy, const uint32_t from_block_num){

    //can only be called with account authority
    require_auth(get_self());

    policies_table _policies_table(get_self(), get_self().value);

    //can only be used once for the initilization of the contract
    check(_policies_table.begin() == _policies_table.end(), "can only set finalizer policy manually for initialization");

    _maybe_set_finalizer_policy(policy, from_block_num);
}

ACTION svnn_ibc::checkproof(const proof& proof){

    //if we have a finality proof, we execute the "heavy" code path
    if (proof.finality_proof.has_value()){
        _check_finality_proof(proof.finality_proof.value(), proof.target_block_proof_of_inclusion);
    }
    else {
        //if we only have a proof of inclusion of the target block, we execute the "light" code path
        _check_target_block_proof_of_inclusion(proof.target_block_proof_of_inclusion, std::nullopt);
    }
}

//temporary : reset the state
ACTION svnn_ibc::clear(){
   require_auth(get_self());
   proofs_table _proofs_table(get_self(), get_self().value);
   policies_table _policies_table(get_self(), get_self().value);
   auto fp_itr = _proofs_table.begin();
   while (fp_itr!= _proofs_table.end()){
       fp_itr = _proofs_table.erase(fp_itr);
   }
   auto p_itr = _policies_table.begin();
   while (p_itr!= _policies_table.end()){
       p_itr = _policies_table.erase(p_itr);
   }
}