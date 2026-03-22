#include "httplib.h"
#include "Log.h"
#include "Database.h"
#include "Str.h"
#include "Str_Format.h"
#include "Fixed_Str.h"

#include <cstdio>
#include <string>
#include <ctime>

using namespace hstl;

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
		try { record.amount = std::stol(clean_amount); }
		catch (...) { return false; }
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

	try { record.amount = std::stol(clean_amount); }
	catch (...) { return false; }

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
	Database db("transactions.db");
	auto db_res = db.init();
	if (!db_res) {
		hstl::log_error("Fatal: {}", db_res.get_err().get_message());
		return 1;
	}
	hstl::log_info("Database initialized successfully.");

	httplib::Server svr;

	auto ret = svr.set_mount_point("/", "./public");
	if (!ret) {
		hstl::log_error("The specified base directory doesn't exist!");
	}
	svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
		res.set_redirect("/index.html");
	});

	svr.Post("/webhook", [&db](const httplib::Request& req, httplib::Response& res) {
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

				// Using our new clean class method!
				auto save_res = db.save_transaction(record);
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

	svr.Get("/api/transactions", [&db](const httplib::Request&, httplib::Response& res) {
		hstl::Str json_data = db.get_transactions_json();
		res.set_content(json_data.c_str(), "application/json");
	});

	svr.Put(R"(/api/transactions/(\d+)/note)", [&db](const httplib::Request& req, httplib::Response& res) {
		int id = std::atoi(req.matches[1].str().c_str());
		hstl::Str_View new_note(req.body.c_str(), req.body.length());

		auto update_res = db.update_note(id, new_note);
		if (update_res) {
			res.status = 200;
			res.set_content("OK", "text/plain");
		}
		else {
			hstl::log_error("Note update failed");
			res.status = 500;
		}
	});

	svr.Delete(R"(/api/transactions/(\d+))", [&db](const httplib::Request& req, httplib::Response& res) {
		int id = std::atoi(req.matches[1].str().c_str());
		auto delete_res = db.delete_transaction(id);

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

	svr.Delete("/api/transactions", [&db](const httplib::Request& req, httplib::Response& res) {
		auto clear_res = db.clear_transactions();
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

	return 0;
}