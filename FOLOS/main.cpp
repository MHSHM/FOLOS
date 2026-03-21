#include "httplib.h"
#include "Log.h"
#include "Str.h"
#include "Result.h"
#include "Str_Format.h"
#include "Fixed_Str.h"
#include "sqlite3.h"

#include <cstdio>
#include <string>

using namespace hstl;

struct TransactionRecord {
	long amount{ 0 };
	hstl::Str currency;
	hstl::Str merchant;
	hstl::Str date;
	hstl::Str category;
	hstl::Str notes;
	hstl::Str raw_sms;
};

hstl::Result<sqlite3*> init_database(const char* db_name) {
	sqlite3* db = nullptr;
	if (sqlite3_open(db_name, &db) != SQLITE_OK) {
		return hstl::Err("Failed to open database: {}", sqlite3_errmsg(db));
	}

	const char* create_table_sql =
		"CREATE TABLE IF NOT EXISTS transactions ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"amount INTEGER,"
		"currency TEXT,"
		"merchant TEXT,"
		"date TEXT,"
		"notes TEXT DEFAULT '',"
		"raw_sms TEXT DEFAULT '',"
		"category TEXT DEFAULT 'Others',"
		"created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

	char* err_msg = nullptr;
	if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
		hstl::Err err("Failed to create table: {}", (const char*)err_msg);
		sqlite3_free(err_msg);
		sqlite3_close(db);
		return err;
	}

	// Attempt to migrate existing databases to include the new column
	// This will naturally fail if the column already exists, which is safe to ignore.
	sqlite3_exec(db, "ALTER TABLE transactions ADD COLUMN category TEXT DEFAULT 'Others';", nullptr, nullptr, nullptr);

	return db;
}

hstl::Result<bool> save_transaction(sqlite3* db, const TransactionRecord& record) {
	const char* insert_sql = "INSERT INTO transactions (amount, currency, merchant, date, category, notes, raw_sms) VALUES (?, ?, ?, ?, ?, ?, ?);";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return hstl::Err("Failed to prepare statement: {}", sqlite3_errmsg(db));
	}

	sqlite3_bind_int64(stmt, 1, record.amount);

	hstl::Str_View currency_v = record.currency.view();
	sqlite3_bind_text(stmt, 2, currency_v.data(), (int)currency_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View merchant_v = record.merchant.view();
	sqlite3_bind_text(stmt, 3, merchant_v.data(), (int)merchant_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View date_v = record.date.view();
	sqlite3_bind_text(stmt, 4, date_v.data(), (int)date_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View category_v = record.category.view();
	sqlite3_bind_text(stmt, 5, category_v.data(), (int)category_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View notes_v = record.notes.view();
	sqlite3_bind_text(stmt, 6, notes_v.data(), (int)notes_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View raw_sms_v = record.raw_sms.view();
	sqlite3_bind_text(stmt, 7, raw_sms_v.data(), (int)raw_sms_v.count(), SQLITE_TRANSIENT);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		hstl::Err err("Failed to execute statement: {}", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return err;
	}

	sqlite3_finalize(stmt);
	return true;
}

void normalize_date(hstl::Str& date_str) {
	if (date_str.view().count() != 5 || date_str.view()[2] != '/') {
		return;
	}

	hstl::Str_View view = date_str.view();

	hstl::Fixed_Str<3> day;
	day.push_range(view.data(), 2);

	hstl::Fixed_Str<3> month;
	month.push_range(view.data() + 3, 2);

	std::time_t t = std::time(nullptr);
	std::tm* now = std::localtime(&t);
	int current_year = now->tm_year + 1900;
	int current_month = now->tm_mon + 1;

	int msg_month = std::atoi(month.c_str());

	if (msg_month == 12 && current_month == 1) {
		current_year--;
	}

	auto formatted = hstl::fmt_fixed_str<16>("{}-{}-{}", current_year, month.view(), day.view());

	date_str.clear();
	date_str.push_range(formatted.c_str(), formatted.count());
}

hstl::Str_View extract_json_value(hstl::Str_View payload, const char* key) {
	char search_key[32];
	std::snprintf(search_key, sizeof(search_key), "\"%s\"", key);

	size_t key_pos = payload.find(search_key);
	if (key_pos == hstl::Str_View::npos) return hstl::Str_View("");

	hstl::Str_View after_key = payload.substr(key_pos, payload.count() - key_pos);
	size_t colon_pos = after_key.find(':');
	if (colon_pos == hstl::Str_View::npos) return hstl::Str_View("");

	hstl::Str_View after_colon = after_key.substr(colon_pos, after_key.count() - colon_pos);
	size_t start_quote = after_colon.find('"');
	if (start_quote == hstl::Str_View::npos) return hstl::Str_View("");

	hstl::Str_View value_area = after_colon.substr(start_quote + 1, after_colon.count() - (start_quote + 1));
	size_t end_quote = value_area.find('"');
	if (end_quote == hstl::Str_View::npos) return hstl::Str_View("");

	return value_area.substr(0, end_quote);
}

hstl::Str escape_json_string(const char* input) {
	hstl::Str out;
	if (!input) return out;

	size_t len = std::strlen(input);
	for (size_t i = 0; i < len; ++i) {
		if (input[i] == '"') out.push_range("\\\"", 2);
		else if (input[i] == '\\') out.push_range("\\\\", 2);
		else if (input[i] == '\n') out.push_range("\\n", 2);
		else if (input[i] == '\r') out.push_range("\\r", 2);
		else if (input[i] == '\t') out.push_range("\\t", 2);
		else out.push(input[i]);
	}
	return out;
}

hstl::Str get_transactions_json(sqlite3* db) {
	hstl::Str json;
	json.push('[');

	const char* query = "SELECT id, amount, currency, merchant, date, created_at, notes, category FROM transactions ORDER BY created_at DESC;";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
		bool first = true;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			if (!first) {
				json.push(',');
			}
			first = false;

			int id = sqlite3_column_int(stmt, 0);
			long amount = sqlite3_column_int64(stmt, 1);
			const char* currency = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
			const char* merchant = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
			const char* date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
			const char* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
			const char* raw_notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
			const char* raw_category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));

			hstl::Str escaped_notes = escape_json_string(raw_notes);
			hstl::Str escaped_category = escape_json_string(raw_category ? raw_category : "Others");

			hstl::fmt(json,
				R"({"id":{},"amount":{},"currency":"{}","merchant":"{}","date":"{}","created_at":"{}","notes":"{}","category":"{}"})",
				id, amount, currency, merchant, date, created_at, escaped_notes.view(), escaped_category.view()
			);
		}
	}
	else {
		hstl::log_error("Failed to query transactions: {}", sqlite3_errmsg(db));
	}

	sqlite3_finalize(stmt);
	json.push(']');
	return json;
}

hstl::Result<bool> delete_transaction(sqlite3* db, int id) {
	const char* delete_sql = "DELETE FROM transactions WHERE id = ?;";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, delete_sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return hstl::Err("Failed to prepare delete statement: {}", sqlite3_errmsg(db));
	}
	sqlite3_bind_int(stmt, 1, id);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		hstl::Err err("Failed to execute delete statement: {}", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return err;
	}

	sqlite3_finalize(stmt);
	return true;
}

hstl::Result<bool> clear_transactions(sqlite3* db) {
	char* err_msg = nullptr;
	if (sqlite3_exec(db, "DELETE FROM transactions;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
		hstl::Err err("Failed to clear transactions: {}", (const char*)err_msg);
		sqlite3_free(err_msg);
		return err;
	}
	sqlite3_exec(db, "DELETE FROM sqlite_sequence WHERE name='transactions';", nullptr, nullptr, nullptr);
	return true;
}

bool is_arabic_message(hstl::Str_View sms) {
	if (sms.find("خصم") != hstl::Str_View::npos) return true;
	if (sms.find("حساب") != hstl::Str_View::npos) return true;
	if (sms.find("جم") != hstl::Str_View::npos) return true;
	if (sms.find("جنيه") != hstl::Str_View::npos) return true;
	return false;
}

bool parse_english_sms(hstl::Str_View sms, TransactionRecord& record) {
	size_t debited_pos = sms.find("debited by");
	if (debited_pos == hstl::Str_View::npos) return false;

	size_t currency_pos = debited_pos + 11;
	hstl::Str_View currency_str = sms.substr(currency_pos, 3);
	record.currency.clear();
	record.currency.push_range(currency_str.data(), currency_str.count());

	size_t amount_start = currency_pos + 3;
	size_t amount_end = amount_start;

	while (amount_end < sms.count() && ((sms[amount_end] >= '0' && sms[amount_end] <= '9') || sms[amount_end] == '.' || sms[amount_end] == ',')) {
		amount_end++;
	}

	hstl::Str_View amount_str = sms.substr(amount_start, amount_end - amount_start);

	std::string clean_amount;
	for (size_t i = 0; i < amount_str.count(); ++i) {
		if (amount_str[i] != ',') clean_amount += amount_str[i];
	}

	if (!clean_amount.empty()) {
		try {
			record.amount = std::stol(clean_amount);
		}
		catch (...) {
			return false;
		}
	}

	size_t slash_pos = hstl::Str_View::npos;
	for (size_t i = amount_end; i < sms.count(); ++i) {
		if (sms[i] == '/') {
			slash_pos = i;
			break;
		}
	}

	if (slash_pos != hstl::Str_View::npos && slash_pos >= 2 && slash_pos + 2 < sms.count()) {
		hstl::Str_View date_str = sms.substr(slash_pos - 2, 5);
		record.date.clear();
		record.date.push_range(date_str.data(), date_str.count());
	}
	else {
		record.date = hstl::Str("Unknown");
	}

	size_t hyphen_pos = sms.find("- ");
	if (hyphen_pos == hstl::Str_View::npos) return false;

	size_t merchant_start = hyphen_pos + 2;
	size_t merchant_end = sms.find(" ref");
	if (merchant_end == hstl::Str_View::npos) merchant_end = sms.find(". For");

	if (merchant_end == hstl::Str_View::npos || merchant_start >= merchant_end) return false;

	hstl::Str_View merchant_str = sms.substr(merchant_start, merchant_end - merchant_start);
	record.merchant.clear();
	record.merchant.push_range(merchant_str.data(), merchant_str.count());

	return true;
}

bool parse_arabic_sms(hstl::Str_View sms, TransactionRecord& record) {
	size_t amount_start = hstl::Str_View::npos;
	for (size_t i = 0; i < sms.count(); ++i) {
		if (sms[i] >= '0' && sms[i] <= '9') {
			amount_start = i;
			break;
		}
	}

	if (amount_start == hstl::Str_View::npos) return false;

	size_t amount_end = amount_start;
	while (amount_end < sms.count() && ((sms[amount_end] >= '0' && sms[amount_end] <= '9') || sms[amount_end] == '.' || sms[amount_end] == ',')) {
		amount_end++;
	}

	hstl::Str_View amount_str = sms.substr(amount_start, amount_end - amount_start);

	std::string clean_amount;
	for (size_t i = 0; i < amount_str.count(); ++i) {
		if (amount_str[i] != ',') clean_amount += amount_str[i];
	}
	if (clean_amount.empty()) return false;

	try {
		record.amount = std::stol(clean_amount);
	}
	catch (...) {
		return false;
	}

	size_t currency_start = amount_end;
	while (currency_start < sms.count() && sms[currency_start] == ' ') currency_start++;

	size_t currency_end = currency_start;
	while (currency_end < sms.count() && sms[currency_end] != ' ') currency_end++;

	if (currency_start < currency_end) {
		hstl::Str_View currency_str = sms.substr(currency_start, currency_end - currency_start);

		if (currency_str.find("جم") != hstl::Str_View::npos || currency_str.find("جنيه") != hstl::Str_View::npos) {
			record.currency.clear();
			record.currency.push_range("EGP", 3);
		}
		else {
			record.currency.clear();
			record.currency.push_range(currency_str.data(), currency_str.count());
		}
	}

	size_t slash_pos = hstl::Str_View::npos;
	for (size_t i = currency_end; i < sms.count(); ++i) {
		if (sms[i] == '/') {
			slash_pos = i;
			break;
		}
	}

	if (slash_pos != hstl::Str_View::npos && slash_pos >= 2 && slash_pos + 2 < sms.count()) {
		hstl::Str_View date_str = sms.substr(slash_pos - 2, 5);
		record.date.clear();
		record.date.push_range(date_str.data(), date_str.count());
	}
	else {
		record.date = hstl::Str("Unknown");
	}

	size_t merchant_start = hstl::Str_View::npos;
	if (slash_pos != hstl::Str_View::npos) {
		for (size_t i = slash_pos; i < sms.count(); ++i) {
			if (sms[i] == ' ') {
				merchant_start = i + 1;
				break;
			}
		}
	}
	else {
		const char* markers[] = { "فى ", "في ", "يوم ", "سحب ", "- " };
		for (const char* marker : markers) {
			size_t pos = sms.find(marker);
			if (pos != hstl::Str_View::npos && pos > currency_end) {
				merchant_start = pos + std::strlen(marker);
				break;
			}
		}
	}

	if (merchant_start == hstl::Str_View::npos) return false;

	size_t merchant_end = sms.find("الرصيد");
	if (merchant_end == hstl::Str_View::npos) merchant_end = sms.find("يمكنك");
	if (merchant_end == hstl::Str_View::npos) merchant_end = sms.count();

	while (merchant_start < merchant_end && (sms[merchant_start] == ' ' || sms[merchant_start] == '-')) {
		merchant_start++;
	}

	hstl::Str_View temp_merchant = sms.substr(merchant_start, merchant_end - merchant_start);
	const char* saheb = "سحب ";
	if (temp_merchant.find(saheb) == 0) {
		merchant_start += std::strlen(saheb);
	}

	while (merchant_end > merchant_start && sms[merchant_end - 1] == ' ') merchant_end--;

	if (merchant_start >= merchant_end) return false;

	hstl::Str_View merchant_str = sms.substr(merchant_start, merchant_end - merchant_start);
	record.merchant.clear();
	record.merchant.push_range(merchant_str.data(), merchant_str.count());

	return true;
}

int main() {
	auto db_res = init_database("transactions.db");
	if (!db_res) {
		hstl::log_error("Fatal: {}", db_res.get_err().get_message());
		return 1;
	}

	sqlite3* db = db_res.get_value();
	hstl::log_info("Database initialized successfully.");

	httplib::Server svr;

	svr.Post("/webhook", [db](const httplib::Request& req, httplib::Response& res) {
		if (!req.has_header("Authorization") || req.get_header_value("Authorization") != "Bearer MySecretToken123") {
			log_error("Rejected request: Invalid or Missing token.");
			res.status = 401;
			return;
		}

		hstl::Str_View payload(req.body.c_str(), req.body.length());
		hstl::Str_View raw_sms = extract_json_value(payload, "text");

		if (raw_sms.count() > 0) {
			TransactionRecord record;
			record.raw_sms.push_range(raw_sms.data(), raw_sms.count());
			record.category.push_range("Others", 6); // Default assignment

			bool success = false;
			if (is_arabic_message(raw_sms)) {
				success = parse_arabic_sms(raw_sms, record);
				if (success) hstl::log_info("Arabic Transaction Recorded:");
			}
			else {
				success = parse_english_sms(raw_sms, record);
				if (success) hstl::log_info("English Transaction Recorded:");
			}

			if (success) {
				normalize_date(record.date);

				hstl::log_info("   Amount:   {} {}", record.amount, record.currency.view());
				hstl::log_info("   Merchant: {}", record.merchant.view());
				hstl::log_info("   Date:     {}", record.date.view());

				auto save_res = save_transaction(db, record);
				if (save_res) hstl::log_info("   -> Successfully saved to database.");
				else hstl::log_error("   -> Failed to save to database: {}", save_res.get_err().get_message());
			}
			else {
				hstl::log_warn("Ignored SMS (Not a recognized transaction format).");
			}
		}

		res.status = 200;
		res.set_content("Processed", "text/plain");
		});

	svr.Get("/api/transactions", [db](const httplib::Request&, httplib::Response& res) {
		hstl::Str json_data = get_transactions_json(db);
		res.set_content(json_data.c_str(), "application/json");
		});

	svr.Put(R"(/api/transactions/(\d+)/note)", [db](const httplib::Request& req, httplib::Response& res) {
		int id = std::atoi(req.matches[1].str().c_str());
		hstl::Str_View new_note(req.body.c_str(), req.body.length());

		const char* update_sql = "UPDATE transactions SET notes = ? WHERE id = ?;";
		sqlite3_stmt* stmt;

		if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_text(stmt, 1, new_note.data(), (int)new_note.count(), SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 2, id);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				res.status = 200;
				res.set_content("OK", "text/plain");
			}
			else {
				hstl::log_error("Note update failed: {}", sqlite3_errmsg(db));
				res.status = 500;
			}
			sqlite3_finalize(stmt);
		}
		else {
			res.status = 500;
		}
		});

	// New API endpoint to handle inline category updates
	svr.Put(R"(/api/transactions/(\d+)/category)", [db](const httplib::Request& req, httplib::Response& res) {
		int id = std::atoi(req.matches[1].str().c_str());
		hstl::Str_View new_category(req.body.c_str(), req.body.length());

		const char* update_sql = "UPDATE transactions SET category = ? WHERE id = ?;";
		sqlite3_stmt* stmt;

		if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_text(stmt, 1, new_category.data(), (int)new_category.count(), SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 2, id);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				res.status = 200;
				res.set_content("OK", "text/plain");
			}
			else {
				hstl::log_error("Category update failed: {}", sqlite3_errmsg(db));
				res.status = 500;
			}
			sqlite3_finalize(stmt);
		}
		else {
			res.status = 500;
		}
	});

	auto ret = svr.set_mount_point("/", "./public");
	if (!ret)
		hstl::log_error("The specified base directory doesn't exist!");

	svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
		res.set_redirect("/index.html");
	});

	svr.Delete(R"(/api/transactions/(\d+))", [db](const httplib::Request& req, httplib::Response& res) {
		int id = std::atoi(req.matches[1].str().c_str());
		auto delete_res = delete_transaction(db, id);
		if (delete_res) {
			res.status = 200;
			res.set_content("Deleted", "text/plain");
		}
		else {
			hstl::log_error("Delete failed: {}", delete_res.get_err().get_message());
			res.status = 500;
			res.set_content("Internal Server Error", "text/plain");
		}
	});

	svr.Delete("/api/transactions", [db](const httplib::Request& req, httplib::Response& res) {
		auto clear_res = clear_transactions(db);
		if (clear_res) {
			res.status = 200;
			res.set_content("Cleared", "text/plain");
		}
		else {
			hstl::log_error("Clear failed: {}", clear_res.get_err().get_message());
			res.status = 500;
			res.set_content("Internal Server Error", "text/plain");
		}
	});

	hstl::log_info("Webhook server starting on http://0.0.0.0:8080...");
	svr.listen("0.0.0.0", 8080);

	sqlite3_close(db);

	return 0;
}