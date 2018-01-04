#ifndef __JSON_HELPER_H__
#define __JSON_HELPER_H__

#include <json/json.h>
#include <string>
#include <vector>
#include <assert.h>
#include <iostream>
#include <curl/curl.h>
#include <sys/time.h>
#include "util.h"

using namespace std;


class CPrintFunTime {
public:
	CPrintFunTime(const string& funName)
		:m_strFunName(funName)
	{
		gettimeofday(&start, NULL);
	}

	~CPrintFunTime()
	{
		gettimeofday(&end, NULL);
		LogDebug("%s use time is %d MicroSeconds", m_strFunName.c_str(), 1000000*(end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec));
	}

private:
	struct timeval start, end;
	string m_strFunName;
};

enum {
	GET = 0,
	POST
};

class CJsonHelper{
public:
	static bool toJson(const char *data, size_t data_len, Json::Value & result)
	{
		//CPrintFunTime tmp("CJsonHelper::toJson(start)");
		if (data == NULL)return false;

		string str(data, data_len);

		vector<string> strvct;
		split(str, strvct);
		for(size_t i = 0; i != strvct.size(); ++i){
			string::size_type pos = strvct[i].find("=");

			if (pos == string::npos || pos == 0) {
				LogWarning("CJsonHelper::toJson(can not find '=', query string: %s)", strvct[i].c_str());
				continue;
				//return false;
			} else {
				Json::Value j_value;
				CURL *curl = curl_easy_init();
				string key = strvct[i].substr(0,pos);
				string value = strvct[i].substr(pos + 1,strvct[i].length() - pos - 1);
				int length = 0;
				char *decode= curl_easy_unescape(curl,value.c_str(),value.size(), &length);
				string decode_value(decode, length);
				curl_free(decode);
				curl_easy_cleanup(curl);
				
				if (key == "json_job" || key == "json_job_array" || key == "op_json" || key == "update_json" || key == "query2_json"
						|| key == "query_field_json" || key == "job_id_list" || key == "sequence_id_list" || key == "ip_list"
								){
					if (decode_value.size() == 0){
						LogWarning("CJsonHelper::toJson(decode_value is empty, query string: %s)", strvct[i].c_str());
						continue;
						//return false;
					}
					if (from_str(decode_value,j_value) == 0){
						if (key == "json_job"){
							if (j_value.isObject() && key == "json_job")
								result[key] = j_value;
							else {
								LogWarning("CJsonHelper::toJson(json is not json, query string: %s)", strvct[i].c_str());
								return false;
							}
						} else {
							if (j_value.isArray())
								result[key] = j_value;
							else {
								LogWarning("CJsonHelper::toJson(%s is not array, query string: %s)", key.c_str(), strvct[i].c_str());
								return false;
							}
						}
					} else {
						LogWarning("CJsonHelper::toJson(from_str failed)");
						return false;
					}
				} else if ( key == "append" || key == "query_step" || key == "step" || key == "new_step" || key == "client_module"
						|| key == "cur_page" || key == "per_page" || key == "time_interval"){
					if (decode_value.size() == 0) {
						LogWarning("CJsonHelper::toJson(decode_value is empty, query string: %s)", strvct[i].c_str());
						continue;
						//return false;
					}
					int nValue = StringToInt(decode_value);
					string strValue = IntToString(nValue);
					if (decode_value != strValue){
						LogWarning("CJsonHelper::toJson(trans to int failed, value is: %s)", decode_value.c_str());
						return false;
					}
					result[key] = nValue;
				} else if (key == "cmd_type" || key == "job_id" || key == "op"
						|| key == "sequence_id" || key == "field" || key == "data"
						|| key == "job_type_in"|| key == "query_key" || key == "author"
						|| key == "status_in" || key == "order_by") {
					if (decode_value.size() == 0 &&  key != "data") {
						LogWarning(
								"CJsonHelper::toJson(decode_value is empty, query string: %s)",
								strvct[i].c_str());
						continue;
					}
					result[key] = decode_value;
				} else {
					LogWarning("CJsonHelper::toJson(invalid key, key: %s)", key.c_str());
					continue;
				}
			}
		}
		return true;
	}

private:
	static void split(const string & str, vector<string> & strvct, const string & splitor = "&")
	{
		string::size_type pos = 0;

		strvct.clear();
		while (true) {
			string::size_type tmp = str.find(splitor, pos);

			if (tmp == string::npos) {
				strvct.push_back(str.substr(pos));
				break;
			} else {
				strvct.push_back(str.substr(pos, tmp - pos));
				pos = tmp + splitor.length(); 
			}
		}
	}

	static int from_str(const string& str,Json::Value & root)
	{
		Json::Reader reader;
		if (reader.parse(str,root,false))
			return 0;
		else {
			return -1;
		}
	}

};

#endif
