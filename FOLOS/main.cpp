#include "httplib.h"
#include "Log.h"
#include "Database.h"
#include "Str.h"
#include "Str_Format.h"
#include "Fixed_Str.h"
#include "SMS_Parser.h"
#include "Result.h"

#include <cstdio>
#include <string>
#include <ctime>

using namespace hstl;

void normalize_date(hstl::Str& date_str)
{
	if (date_str.view().count() != 5 || date_str.view()[2] != '/')
		return;
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

	if (msg_month == 12 && current_month == 1)
		current_year--;
	auto formatted = hstl::fmt_fixed_str<16>("{}-{}-{}", current_year, month.view(), day.view());

	date_str.clear();
	date_str.push_range(formatted.c_str(), formatted.count());
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
	if (!ret)
		hstl::log_error("The specified base directory doesn't exist!");

	svr.Get("/", [](const httplib::Request&, httplib::Response& res)
	{
		res.set_redirect("/index.html");
	});

	svr.Post("/webhook", [&db](const httplib::Request& req, httplib::Response& res)
	{
		if (!req.has_header("Authorization") || req.get_header_value("Authorization") != "Bearer MySecretToken123")
		{
			log_error("Rejected request: Invalid or Missing token.");
			res.status = 401;
			return;
		}

		hstl::Str_View payload(req.body.c_str(), req.body.length());
		if (auto raw_sms_res = SMS_Parser::extract_raw_json_string(payload, "text"); raw_sms_res)
		{
			hstl::Str_View raw_sms = raw_sms_res.get_value();
			TransactionRecord record;
			record.raw_sms.push_range(raw_sms.data(), raw_sms.count());
			
			if (auto ai_res = SMS_Parser::parse_sms_via_ai(raw_sms, &record); ai_res)
			{
				normalize_date(record.date);

				hstl::log_info("Transaction Detected by AI:");
				hstl::log_info("   Amount:   {} {}", record.amount, record.currency.view());
				hstl::log_info("   Merchant: {}", record.merchant.view());
				hstl::log_info("   Date:     {}", record.date.view());

				auto save_res = db.save_transaction(record);
				if (save_res)
					hstl::log_info("   -> Successfully saved to database.");
				else
					hstl::log_error("   -> Failed to save to database: {}", save_res.get_err().get_message());
			}
			else
			{
				hstl::log_warn("AI Parsing Failed: {}", ai_res.get_err().get_message());
			}
		}
		else
		{
			hstl::log_error("Webhook error: {}", raw_sms_res.get_err().get_message());
		}

		res.status = 200;
		res.set_content("Processed", "text/plain");
	});

	svr.Get("/api/transactions", [&db](const httplib::Request&, httplib::Response& res)
	{
		hstl::Str json_data = db.get_transactions_json();
		res.set_content(json_data.c_str(), "application/json");
	});

	svr.Put(R"(/api/transactions/(\d+)/note)", [&db](const httplib::Request& req, httplib::Response& res)
	{
		int id = std::atoi(req.matches[1].str().c_str());
		hstl::Str_View new_note(req.body.c_str(), req.body.length());

		auto update_res = db.update_note(id, new_note);
		if (update_res) {
			res.status = 200;
			res.set_content("OK", "text/plain");
		}
		else
		{
			hstl::log_error("Note update failed");
			res.status = 500;
		}
	});

	svr.Delete(R"(/api/transactions/(\d+))", [&db](const httplib::Request& req, httplib::Response& res)
	{
		int id = std::atoi(req.matches[1].str().c_str());
		auto delete_res = db.delete_transaction(id);

		if (delete_res)
		{
			res.status = 200;
			res.set_content("Deleted", "text/plain");
		}
		else
		{
			hstl::log_error("Delete failed: {}", delete_res.get_err().get_message());
			res.status = 500;
			res.set_content("Internal Server Error", "text/plain");
		}
	});

	svr.Delete("/api/transactions", [&db](const httplib::Request& req, httplib::Response& res)
	{
		auto clear_res = db.clear_transactions();
		if (clear_res)
		{
			res.status = 200;
			res.set_content("Cleared", "text/plain");
		}
		else
		{
			hstl::log_error("Clear failed: {}", clear_res.get_err().get_message());
			res.status = 500;
			res.set_content("Internal Server Error", "text/plain");
		}
	});

	hstl::log_info("Webhook server starting on http://0.0.0.0:8080...");
	svr.listen("0.0.0.0", 8080);

	return 0;
}