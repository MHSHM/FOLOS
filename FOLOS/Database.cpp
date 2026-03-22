#include "Database.h"

#include <Log.h>

Database::Database(const char* db_name):
	db_name(db_name)
{}

Database::~Database()
{
	if (db)
	{
		sqlite3_close(db);
	}
}

hstl::Result<bool> Database::init()
{
	if (sqlite3_open(db_name.c_str(), &db) != SQLITE_OK)
	{
		return hstl::Err("Failed to open database: {}", sqlite3_errmsg(db));
	}

	hstl::Str_View create_table_sql =
		"CREATE TABLE IF NOT EXISTS transactions ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"amount INTEGER,"
		"currency TEXT,"
		"merchant TEXT,"
		"date TEXT,"
		"notes TEXT DEFAULT '',"
		"raw_sms TEXT DEFAULT '',"
		"created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

	char* err_msg = nullptr;
	if (sqlite3_exec(db, create_table_sql.data(), nullptr, nullptr, &err_msg) != SQLITE_OK)
	{
		hstl::Err err("Failed to create table: {}", (const char*)err_msg);
		sqlite3_free(err_msg);
		return err;
	}

	return true;
}

hstl::Result<bool> Database::save_transaction(const TransactionRecord& record)
{
	hstl::Str_View insert_sql = "INSERT INTO transactions (amount, currency, merchant, date, notes, raw_sms) VALUES (?, ?, ?, ?, ?, ?);";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, insert_sql.data(), -1, &stmt, nullptr) != SQLITE_OK)
		return hstl::Err("Failed to prepare statement: {}", sqlite3_errmsg(db));

	sqlite3_bind_int64(stmt, 1, record.amount);

	hstl::Str_View currency_v = record.currency.view();
	sqlite3_bind_text(stmt, 2, currency_v.data(), (int)currency_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View merchant_v = record.merchant.view();
	sqlite3_bind_text(stmt, 3, merchant_v.data(), (int)merchant_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View date_v = record.date.view();
	sqlite3_bind_text(stmt, 4, date_v.data(), (int)date_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View notes_v = record.notes.view();
	sqlite3_bind_text(stmt, 5, notes_v.data(), (int)notes_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View raw_sms_v = record.raw_sms.view();
	sqlite3_bind_text(stmt, 6, raw_sms_v.data(), (int)raw_sms_v.count(), SQLITE_TRANSIENT);

	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		hstl::Err err("Failed to execute statement: {}", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return err;
	}

	sqlite3_finalize(stmt);
	return true;
}

hstl::Str Database::escape_json_string(hstl::Str_View input)
{
	hstl::Str out;

	for (size_t i = 0; i < input.count(); ++i)
	{
		switch (input[i])
		{
		case '"':  out.push_range("\\\"", 2); break;
		case '\\': out.push_range("\\\\", 2); break;
		case '\n': out.push_range("\\n", 2);  break;
		case '\r': out.push_range("\\r", 2);  break;
		case '\t': out.push_range("\\t", 2);  break;
		default:   out.push(input[i]);        break;
		}
	}

	return out;
}

hstl::Str Database::get_transactions_json()
{
	hstl::Str json;
	json.push('[');

	const char* query = "SELECT id, amount, currency, merchant, date, created_at, notes FROM transactions ORDER BY created_at DESC;";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK)
	{
		bool skip_adding_comma = true;
		while (sqlite3_step(stmt) == SQLITE_ROW)
		{
			if (!skip_adding_comma)
				json.push(',');
			skip_adding_comma = false;

			int id = sqlite3_column_int(stmt, 0);
			long amount = sqlite3_column_int64(stmt, 1);
			const char* currency = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
			const char* merchant = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
			const char* date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
			const char* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
			const char* raw_notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

			hstl::fmt(json,
				R"({"id":{},"amount":{},"currency":"{}","merchant":"{}","date":"{}","created_at":"{}","notes":"{}"})",
				id, amount, currency, merchant, date, created_at, escape_json_string(raw_notes).view()
			);
		}
	}
	else
	{
		hstl::log_error("Failed to query transactions: {}", sqlite3_errmsg(db));
	}

	sqlite3_finalize(stmt);
	json.push(']');
	return json;
}

hstl::Result<bool> Database::update_note(int id, hstl::Str_View note)
{
	const char* update_sql = "UPDATE transactions SET notes = ? WHERE id = ?;";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr) != SQLITE_OK)
		return hstl::Err("Failed to prepare note update: {}", sqlite3_errmsg(db));

	sqlite3_bind_text(stmt, 1, note.data(), (int)note.count(), SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 2, id);

	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		hstl::Err err("Failed to execute update statement: {}", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return err;
	}

	sqlite3_finalize(stmt);
	return true;
}

hstl::Result<bool> Database::delete_transaction(int id)
{
	const char* delete_sql = "DELETE FROM transactions WHERE id = ?;";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, delete_sql, -1, &stmt, nullptr) != SQLITE_OK)
		return hstl::Err("Failed to prepare delete statement: {}", sqlite3_errmsg(db));

	sqlite3_bind_int(stmt, 1, id);

	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		hstl::Err err("Failed to execute delete statement: {}", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return err;
	}

	sqlite3_finalize(stmt);
	return true;
}

hstl::Result<bool> Database::clear_transactions()
{
	// NOTE: DELETE is a static query, it doesn't have any some sort of external variables that need to be bound
	// so we can directly execute it without preparing a statement.

	char* err_msg = nullptr;
	if (sqlite3_exec(db, "DELETE FROM transactions;", nullptr, nullptr, &err_msg) != SQLITE_OK)
	{
		hstl::Err err("Failed to clear transactions: {}", (const char*)err_msg);
		sqlite3_free(err_msg);
		return err;
	}
	sqlite3_exec(db, "DELETE FROM sqlite_sequence WHERE name='transactions';", nullptr, nullptr, nullptr);
	return true;
}