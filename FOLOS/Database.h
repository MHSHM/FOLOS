#pragma once

#include "sqlite3.h"
#include "Str.h"
#include "Result.h"

struct TransactionRecord
{
	long amount{0};
	hstl::Str currency;
	hstl::Str merchant;
	hstl::Str date;
	hstl::Str notes;
	hstl::Str raw_sms;
};

class Database
{
public:
	Database(const char* db_name);
	~Database();

	hstl::Result<bool> init();
	hstl::Result<bool> save_transaction(const TransactionRecord& record);
	hstl::Str get_transactions_json();
	hstl::Result<bool> update_note(int id, hstl::Str_View note);
	hstl::Result<bool> delete_transaction(int id);
	hstl::Result<bool> clear_transactions();

private:
	sqlite3* db{nullptr};
	hstl::Str db_name;
	hstl::Str escape_json_string(hstl::Str_View input);
};