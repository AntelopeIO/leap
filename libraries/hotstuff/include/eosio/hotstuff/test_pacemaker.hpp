#pragma once
#include <eosio/hotstuff/base_pacemaker.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

namespace eosio { namespace hotstuff {

	class test_pacemaker : public base_pacemaker {

	public:

		//class-specific functions

		class indexed_qc_chain{

		public:

			name _name;

			bool _active = true;

			qc_chain* _qc_chain = NULL; //todo : use smart pointer

			uint64_t by_name()const{return _name.to_uint64_t();};

        	~indexed_qc_chain(){

        		//if (_qc_chain == NULL) delete _qc_chain;

        		//_qc_chain = NULL;

        	};

		};

		struct by_name_id{};

		typedef multi_index_container<
			indexed_qc_chain, 
			indexed_by<
				ordered_unique<
					tag<by_name_id>,
					BOOST_MULTI_INDEX_CONST_MEM_FUN(indexed_qc_chain, uint64_t, by_name)
				>
			>
		> qc_chain_type;

		qc_chain_type _qcc_store;


/*		void send_hs_proposal_msg(hs_proposal_message msg);
		void send_hs_vote_msg(hs_vote_message msg);
		void send_hs_new_block_msg(hs_new_block_message msg);
		void send_hs_new_view_msg(hs_new_view_message msg);*/

		using hotstuff_message = std::variant<hs_proposal_message, hs_vote_message, hs_new_block_message, hs_new_view_message>;

	   	//void init(std::vector<name> unique_replicas);

	   	void set_proposer(name proposer);

	   	void set_leader(name leader);
	   	
	   	void set_next_leader(name next_leader);

	   	void set_finalizers(std::vector<name> finalizers);

	    void set_current_block_id(block_id_type id);

	    void set_quorum_threshold(uint32_t threshold);

	    void propagate();

	    //indexed_qc_chain get_qc_chain(name replica);

        //~test_pacemaker(){};

		//base_pacemaker interface functions

       	name get_proposer();
       	name get_leader();
       	name get_next_leader();
       	std::vector<name> get_finalizers();

       	block_id_type get_current_block_id();

       	uint32_t get_quorum_threshold();
        
        void register_listener(name name, qc_chain& qcc);
        void unregister_listener(name name);

        void beat();

		void send_hs_proposal_msg(hs_proposal_message msg);
		void send_hs_vote_msg(hs_vote_message msg);
		void send_hs_new_block_msg(hs_new_block_message msg);
		void send_hs_new_view_msg(hs_new_view_message msg);

      	void on_hs_vote_msg(hs_vote_message msg); //confirmation msg event handler
      	void on_hs_proposal_msg(hs_proposal_message msg); //consensus msg event handler
      	void on_hs_new_view_msg(hs_new_view_message msg); //new view msg event handler
      	void on_hs_new_block_msg(hs_new_block_message msg); //new block msg event handler

	private :

		std::vector<hotstuff_message> _message_queue;
		std::vector<hotstuff_message> _pending_message_queue;

	    name _proposer;
	    name _leader;
	    name _next_leader;

	    std::vector<name> _finalizers;

	    block_id_type _current_block_id;

		std::vector<name> _unique_replicas;

		uint32_t _quorum_threshold = 15; //todo : calculate from schedule 


	};

}}