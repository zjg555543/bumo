#include<cross/challenge_manager.h>
#define CHALLENGE_HEAD_SEQ "challenge_head_seq"
#define CHALLENGE_WITHDRAWAL_SEQ "challenge_withdrawal_seq"
namespace bumo {

	ChallengeSubmitHead::ChallengeSubmitHead() :
		chain_head_seq_(0){}

	ChallengeSubmitHead::~ChallengeSubmitHead(){}
	void ChallengeSubmitHead::InitSeq(){
		auto db = Storage::Instance().keyvalue_db();
		std::string str;
		Json::Value args;
		if (!db->Get(CHALLENGE_HEAD_SEQ, str)) {
			args["chain_seq"] = chain_head_seq_;
			db->Put(CHALLENGE_HEAD_SEQ, args.toFastString());
		}
		else{
			args.fromString(str.c_str());
			chain_head_seq_ = args["chain_seq"].asInt64();
		}
	}


	ChallengeWithdrawal::ChallengeWithdrawal() :
		chain_withdrawal_seq_(0){}

	ChallengeWithdrawal::~ChallengeWithdrawal(){}
	void ChallengeWithdrawal::InitSeq(){
		auto db = Storage::Instance().keyvalue_db();
		std::string str;
		Json::Value args;
		if (!db->Get(CHALLENGE_WITHDRAWAL_SEQ, str)) {
			args["chain_seq"] = chain_withdrawal_seq_;
			db->Put(CHALLENGE_WITHDRAWAL_SEQ, args.toFastString());
		}
		else{
			args.fromString(str.c_str());
			chain_withdrawal_seq_ = args["chain_seq"].asInt64();
		}
	}

	ChallengeManager::ChallengeManager() :
		enabled_(false),
		thread_ptr_(NULL){
		challenge_submit_head_ = std::make_shared<ChallengeSubmitHead>();
		challenge_withdrawal_ = std::make_shared<ChallengeWithdrawal>();
	}

	ChallengeManager::~ChallengeManager(){
		if (thread_ptr_){
			delete thread_ptr_;
			thread_ptr_ = NULL;
		}
	}

	void ChallengeManager::InitSeq(){
		challenge_submit_head_->InitSeq();
		challenge_withdrawal_->InitSeq();
	}

	bool ChallengeManager::Initialize() {
		enabled_ = true;
		thread_ptr_ = new utils::Thread(this);
		if (!thread_ptr_->Start("ChallengeManager")) {
			return false;
		}

		InitSeq();
		return true;
	}

	bool ChallengeManager::Exit(){
		enabled_ = false;
		if (thread_ptr_) {
			thread_ptr_->JoinWithStop();
		}
		return true;
	}

	void ChallengeManager::Run(utils::Thread *thread) {
		while (enabled_){
			utils::Sleep(10);
			int64_t current_time = utils::Timestamp::HighResolution();

		}
	}

	void ChallengeManager::ChallengeNotify(const protocol::MessageChannel &message_channel){
		switch (message_channel.msg_type()){
		case protocol::MESSAGE_CHANNEL_CHALLENGE_HEAD:{
														  HandleChallengeSubmitHead(message_channel);
														  break;
		}
		case protocol::MESSAGE_CHANNEL_CHALLENGE_WITHDRAWAL:{
																HandleChallengeWithdrawal(message_channel);
																break;
		}
		default:
			break;
		}
	}

	void ChallengeManager::HandleChallengeSubmitHead(const protocol::MessageChannel &message_channel){

	}

	void ChallengeManager::HandleChallengeWithdrawal(const protocol::MessageChannel &message_channel){

	}

}