#ifndef SQLPOOL_H
#define SQLPOOL_H

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <iostream>
#include "Locker.h"
#include "Log.h"

class ConnectionPool
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接数
	void DestroyPool();					 //销毁所有连接

	static ConnectionPool *GetInstance(); //单例模式

	void init(string _url, string _user, string _passWord, string _dataBaseName, int _port, int _maxConn); 

public:
	string url;			 //主机地址
	string port;		 //数据库端口号
	string user;		 //登陆数据库用户名
	string passWord;	 //登陆数据库密码
	string dataBaseName; //使用数据库名

private:
	ConnectionPool();
	~ConnectionPool();

	int maxConn;  //最大连接数
	int curConn;  //当前已使用的连接数
	int freeConn; //当前空闲的连接数
	Locker mutex;
	list<MYSQL*> connList; //连接池
	Sem reserve;
};

class connectionRAII //RAII
{
public:
	connectionRAII(MYSQL **con, ConnectionPool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	ConnectionPool *poolRAII;
};

#endif
