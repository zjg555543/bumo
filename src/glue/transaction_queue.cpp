/*
bumo is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

bumo is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with bumo.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "transaction_queue.h"
#include <ledger/ledger_manager.h>
#include <algorithm>

namespace bumo {

	int64_t const QUEUE_TRANSACTION_TIMEOUT = 60 * utils::MICRO_UNITS_PER_SEC;

	TransactionQueue::TransactionQueue(uint32_t queue_limit, uint32_t account_txs_limit)
		: queue_(PriorityCompare{ *this }),
		queue_limit_(queue_limit),
		account_txs_limit_(account_txs_limit)
	{
	}

	TransactionQueue::~TransactionQueue(){}

	
	std::pair<bool, TransactionFrm::pointer> TransactionQueue::Remove(QueueByAddressAndNonce::iterator& account_it, QueueByNonce::iterator& tx_it, bool del_empty){
		TransactionFrm::pointer ptr = nullptr;
		ptr = *tx_it->second.first;
		queue_.erase(tx_it->second.first);
		time_queue_.erase(tx_it->second.second);
		account_it->second.erase(tx_it);
		queue_by_hash_.erase(ptr->GetContentHash());

		if (del_empty && account_it->second.empty()){
			account_nonce_.erase(account_it->first);
			queue_by_address_and_nonce_.erase(account_it);
		}
		return std::move(std::make_pair(true, ptr));
	}
	

	std::pair<bool, TransactionFrm::pointer> TransactionQueue::Remove(const std::string& account_address,const int64_t& nonce){
		TransactionFrm::pointer ptr = nullptr;
		auto account_it =queue_by_address_and_nonce_.find(account_address);
		if (account_it != queue_by_address_and_nonce_.end()){
			auto tx_it = account_it->second.find(nonce);
			if (tx_it != account_it->second.end()){
				ptr = *tx_it->second.first;
				queue_.erase(tx_it->second.first);
				time_queue_.erase(tx_it->second.second);
				account_it->second.erase(tx_it);
				queue_by_hash_.erase(ptr->GetContentHash());

				if (account_it->second.empty()){
					queue_by_address_and_nonce_.erase(account_it);
					account_nonce_.erase(account_address);
				}
				return std::move(std::make_pair(true, ptr));
			}
		}
		return std::move(std::make_pair(false, ptr));
	}
	
	void TransactionQueue::Insert(TransactionFrm::pointer const& tx){
		// Insert into queue
		auto inserted = queue_by_address_and_nonce_[tx->GetSourceAddress()].insert(std::make_pair(tx->GetNonce(), std::make_pair(PriorityQueue::iterator(), TimeQueue::iterator())));
		PriorityQueue::iterator left = queue_.emplace(tx);
		TimeQueue::iterator right = time_queue_.emplace(tx);
		inserted.first->second.first = left;
		inserted.first->second.second = right;
		queue_by_hash_[tx->GetContentHash()]=tx;
	}

	bool TransactionQueue::Import(TransactionFrm::pointer tx, const int64_t& cur_source_nonce,Result &result){
		utils::WriteLockGuard g(lock_);
		bool inserted = false;
		bool replace = false;
		uint32_t account_txs_size = 0;

		account_nonce_[tx->GetSourceAddress()] = cur_source_nonce;

		LOG_TRACE("Import account(%s) transaction(%s) nonce(" FMT_I64 ") fee(" FMT_I64 ")", tx->GetSourceAddress().c_str(), utils::String::BinToHexString(tx->GetContentHash()).c_str(), tx->GetNonce(), tx->GetFee());
		auto account_it = queue_by_address_and_nonce_.find(tx->GetSourceAddress());
		if (account_it != queue_by_address_and_nonce_.end()) {

			account_txs_size = account_it->second.size();

			auto tx_it = account_it->second.find(tx->GetNonce());
			if (tx_it != account_it->second.end()){

				if (tx->GetFee() > (*tx_it->second.first)->GetFee()) {
					//remove transaction for replace ,and after insert
					std::string drop_hash = (*tx_it->second.first)->GetContentHash();
					Remove(account_it, tx_it);
					replace = true;
					account_txs_size--;
					LOG_TRACE("Remove transaction(%s) for replace by transaction(%s) of account(%s) fee(" FMT_I64 ") nonce(" FMT_I64 ") in queue", utils::String::BinToHexString(drop_hash).c_str(), utils::String::BinToHexString(tx->GetContentHash()).c_str(), tx->GetSourceAddress().c_str(), tx->GetFee(), tx->GetNonce());
				}
				else{
					//Discard new transaction
					std::string error_desc = utils::String::Format("Discard transaction(%s) of account(%s) fee(" FMT_I64 ") nonce(" FMT_I64 ") because of lower fee  in queue", utils::String::BinToHexString(tx->GetContentHash()).c_str(), tx->GetSourceAddress().c_str(), tx->GetFee(), tx->GetNonce());
					LOG_ERROR("%s", error_desc.c_str());
					result.set_code(protocol::ERRCODE_TX_INSERT_QUEUE_FAIL);
					result.set_desc(error_desc);
					return inserted;
				}
			}
		}

		if (replace || account_txs_size < account_txs_limit_) {
			Insert(tx);	
			inserted = true;
			//todo...
			while (queue_.size() > queue_limit_) {
				TransactionFrm::pointer t = *queue_.rbegin();
				Remove(t->GetSourceAddress(), t->GetNonce());

				std::string error_desc = utils::String::Format("Discard lowest transaction(%s) of account(%s) fee(" FMT_I64 ") nonce(" FMT_I64 ")  in queue", utils::String::BinToHexString(t->GetContentHash()).c_str(), t->GetSourceAddress().c_str(), t->GetFee(), t->GetNonce());
				LOG_TRACE("%s", error_desc.c_str());
				if (t->GetContentHash() == tx->GetContentHash()){
					result.set_code(protocol::ERRCODE_TX_INSERT_QUEUE_FAIL);
					result.set_desc(error_desc);
					LOG_ERROR("%s", error_desc.c_str());
					inserted = false;
				}
			}
		}

		if (account_txs_size >= account_txs_limit_){
			inserted = false;
			std::string error_desc = utils::String::Format(" transaction(%s) of account(%s) fee(" FMT_I64 ") nonce(" FMT_I64 ") exceed txs limit of per account in queue", utils::String::BinToHexString(tx->GetContentHash()).c_str(), tx->GetSourceAddress().c_str(), tx->GetFee(), tx->GetNonce());
			result.set_code(protocol::ERRCODE_TX_INSERT_QUEUE_FAIL);
			result.set_desc(error_desc);
			LOG_ERROR("%s", error_desc.c_str());
		}

		return inserted;
	}

	protocol::TransactionEnvSet TransactionQueue::TopTransaction(uint32_t limit){
		protocol::TransactionEnvSet set;
		std::unordered_map<std::string, int64_t> topic_seqs;
		std::unordered_map<std::string, int64_t> break_nonce_accounts;
		int64_t last_seq = LedgerManager::Instance().GetLastClosedLedger().seq();
		utils::WriteLockGuard g(lock_);
		uint32_t i = 0;
		
		for (auto t = queue_.begin(); set.txs().size() < limit && t != queue_.end(); ++t) {
			const TransactionFrm::pointer& tx = *t;
			if (set.ByteSize() + tx->GetTransactionEnv().ByteSize() >= General::TXSET_LIMIT_SIZE)
				break;
			
			if (break_nonce_accounts.find(tx->GetSourceAddress()) == break_nonce_accounts.end()) {

				int64_t last_seq = 0;
				do {
					//find this cache
					auto this_iter = topic_seqs.find(tx->GetSourceAddress());
					if (this_iter != topic_seqs.end()) {
						last_seq = this_iter->second;
						break;
					}

					last_seq = account_nonce_[tx->GetSourceAddress()];

				} while (false);

				if (tx->GetNonce() > last_seq + 1) {
					break_nonce_accounts[tx->GetSourceAddress()] = last_seq + 1;
					continue;
				}

				topic_seqs[tx->GetSourceAddress()] = tx->GetNonce();

				*set.add_txs() = tx->GetProtoTxEnv();

				i++;
				LOG_TRACE("top(%u) addr(%s) tx(%s) nonce(" FMT_I64 ") fee(" FMT_I64 ") last seq(" FMT_I64 ")", i, tx->GetSourceAddress().c_str(), utils::String::BinToHexString(tx->GetContentHash()).c_str(), tx->GetNonce(), tx->GetFee(), last_seq);
			}
		}

		return std::move(set);
	}

	uint32_t TransactionQueue::RemoveTxs(const protocol::TransactionEnvSet& set, bool close_ledger){
		utils::WriteLockGuard g(lock_);
		uint32_t ret = 0;
		uint32_t i = 0;
		int64_t last_seq = LedgerManager::Instance().GetLastClosedLedger().seq();

		for (int i = 0; i < set.txs_size(); i++) {
			auto txproto = set.txs(i);
			std::string source_address = txproto.transaction().source_address();
			int64_t nonce = txproto.transaction().nonce();
			std::pair<bool, TransactionFrm::pointer> result = Remove(source_address, nonce);
			if (result.first) ++ret;
			i++;
			LOG_TRACE("RemoveTxs close_ledger_flag(%d) (%u) removed(%d) addr(%s) nonce(" FMT_I64 ") fee(" FMT_I64 ") last seq(" FMT_I64 ")",
				(int)close_ledger, i, (int)result.first, source_address.c_str(), nonce, (int64_t)txproto.transaction().fee(), last_seq);

			//update system account nonce
			auto it = account_nonce_.find(source_address);
			if (close_ledger && it != account_nonce_.end() && it->second < nonce)
				it->second = nonce;
		}

		LOG_TRACE("RemoveTxs after queue size(%u)", queue_.size());
		return ret;
	}

	void TransactionQueue::RemoveTxs(std::vector<TransactionFrm::pointer>& txs, bool close_ledger){
		utils::WriteLockGuard g(lock_);
		uint32_t i = 0;
		int64_t last_seq = LedgerManager::Instance().GetLastClosedLedger().seq();
		for (auto it = txs.begin(); it != txs.end(); it++){
			std::string source_address = (*it)->GetSourceAddress();
			int64_t nonce = (*it)->GetNonce();

			auto result = Remove(source_address, nonce);
			i++;
			LOG_TRACE("RemoveTxs close_ledger_flag(%d) (%u) removed(%d) addr(%s) tx(%s), nonce(" FMT_I64 ") fee(" FMT_I64 ") last seq(" FMT_I64 ")", 
				(int)close_ledger, i, (int)result.first, (*it)->GetSourceAddress().c_str(),
				utils::String::BinToHexString((*it)->GetContentHash()).c_str(), (*it)->GetNonce(), (*it)->GetFee(), last_seq);

			//update system account nonce
			auto iter = account_nonce_.find(source_address);
			if (close_ledger && iter != account_nonce_.end() && iter->second < nonce)
				iter->second = nonce;
		}
		LOG_TRACE("RemoveTxs after queue size(%u)", queue_.size());
	}

	void TransactionQueue::SafeRemoveTx(const std::string& account_address, const int64_t& nonce) {
		utils::WriteLockGuard g(lock_);
		std::pair<bool, TransactionFrm::pointer> result = Remove(account_address, nonce);
	}



	void TransactionQueue::CheckTimeout(int64_t current_time, std::vector<TransactionFrm::pointer>& timeout_txs){
		utils::ReadLockGuard g(lock_);
		for (auto it = time_queue_.begin(); it != time_queue_.end();it++){
			if (!(*it)->CheckTimeout(current_time - QUEUE_TRANSACTION_TIMEOUT))
				break;
			timeout_txs.emplace_back(*it);
		}
	}

	void TransactionQueue::CheckTimeoutAndDel(int64_t current_time,std::vector<TransactionFrm::pointer>& timeout_txs){
		utils::WriteLockGuard g(lock_);
		int64_t last_seq = LedgerManager::Instance().GetLastClosedLedger().seq();		
		while (!time_queue_.empty()){
			auto it =time_queue_.begin();
			if (!(*it)->CheckTimeout(current_time - QUEUE_TRANSACTION_TIMEOUT))
				break;
			timeout_txs.emplace_back(*it);
			std::string account_address = (*it)->GetSourceAddress();
			int64_t nonce = (*it)->GetNonce();
			Remove(account_address, nonce);
		}
		LOG_TRACE("CheckTimeoutAndDel last seq(" FMT_I64 ") number(%u)", last_seq, timeout_txs.size());
	}

	bool TransactionQueue::IsExist(const TransactionFrm::pointer& tx){
		utils::ReadLockGuard g(lock_);
		auto account_it1 = queue_by_address_and_nonce_.find(tx->GetSourceAddress());
		if (account_it1 != queue_by_address_and_nonce_.end()){
			auto tx_it = account_it1->second.find(tx->GetNonce());
			if (tx_it != account_it1->second.end()){
				TransactionFrm::pointer t = *tx_it->second.first;
				if (t->GetContentHash() == tx->GetContentHash()){
					return true;
				}
			}
		}

		return false;
	}

	bool TransactionQueue::IsExist(const std::string& hash){
		utils::ReadLockGuard g(lock_);
		auto it = queue_by_hash_.find(hash);
		if (it != queue_by_hash_.end()){
			return true;
		}
		return false;
	}

	size_t TransactionQueue::Size() {
		utils::ReadLockGuard g(lock_);
		return queue_.size();
	}

	void TransactionQueue::Query(const uint32_t& num, std::vector<TransactionFrm::pointer>& txs){
		utils::ReadLockGuard g(lock_);
		uint32_t count = 0;

		for (auto it = queue_.begin(); it != queue_.end() && count < num; it++) {
			txs.push_back(*it);
			count++;
		}
	}

	bool TransactionQueue::Query(const std::string& hash, TransactionFrm::pointer& tx){
		utils::ReadLockGuard g(lock_);
		auto it = queue_by_hash_.find(hash);
		if (it != queue_by_hash_.end()){
			tx = it->second;
			return true;
		}
		return false;
	}
}

