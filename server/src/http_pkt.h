#ifndef __HTTP_PKT_H__
#define __HTTP_PKT_H__

#include "tfc_base_http.h"

#include "util.h"
#include <string>

using namespace tfc::http;
using namespace mtnc_lib;

class CHttpReqPkt {
public:
	CHttpReqPkt(char *data, size_t data_len)
		:_data(data)
		,_body(NULL)
		,_query_string(NULL)
		,_data_len((int)data_len)
		,_qs_len(-1)
	{}

	~CHttpReqPkt()
	{
	   	if (_query_string != NULL){
			delete _query_string;
			_query_string = NULL;
		}
	}

	int Init()
	{
		if (_http_parse.Init(_data, _data_len) != 0) {
			return -1;
		}
		_body = _data + GetHeadLength();

		const char *uri = _http_parse.HttpUri();
		if(uri == NULL){
			return -2;
		}

		while (*uri != '?' && *uri != 0)
			uri++;

		if (*uri == '?') {
			uri++;
			//if(*uri == 0) return 0;
			_qs_len = strlen(uri);
			_query_string = new char[_qs_len + 1];
			_query_string[_qs_len] = 0;
			memcpy(&_query_string[0], &uri[0], _qs_len);
		}
		return 0; 
	}

	int GetMethod()
	{
		return _http_parse.HttpMethod();
	}

	int GetBodyLength()
	{
		return _http_parse.BodyLength();
	}

	int GetHeadLength()
	{
		return _http_parse.HeadLength();
	}

	const char* GetUri()
	{
		return _http_parse.HttpUri();
	}

	const char* GetQueryString()
	{
		return _query_string;
	}

	int GetQueryStringLength()
	{
		return (int)_qs_len;
	}

	const char* GetBodyContent()
	{
		if (_http_parse.BodyLength() == 0 || HTTP_CONTENT_L_NFOUND == _http_parse.BodyLength() ){
		   return NULL;
		} else {
			return _body;
		}		
	}

private:
	char                *_data, *_body, *_query_string;
	int                 _data_len, _qs_len;
	CHttpParse          _http_parse;
};

class CHttpRspPkt {
public:
	CHttpRspPkt (int code, string reason, const char *body = NULL, size_t body_len = 0)
	{
		string body_str(body, body_len);		

		_http_pkg += "HTTP/1.1 ";
		_http_pkg += IntToString(code);
		_http_pkg += " ";
		_http_pkg += reason;
		_http_pkg += "\r\n";
		//if (body_len != 0){
			_http_pkg += "Content-Type: text/plain\r\n";
			_http_pkg += "Content-Length: ";
			_http_pkg += IntToString(body_len);
			_http_pkg += "\r\n";
		//}
		_http_pkg += "\r\n";
		_http_pkg += body_str;
	}

	~CHttpRspPkt(){}

	const char* Head()
	{
		if (Length() > 0)
			return _http_pkg.c_str();
		else return NULL;
	}

	size_t Length()
	{
		return _http_pkg.length();
	}

private:
	string _http_pkg;
};

#endif
