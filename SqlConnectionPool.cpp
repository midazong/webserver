#include "SqlConnectionPool.h"

ConnectionPool::ConnectionPool()
{
	curConn = 0;
	freeConn = 0;
}

ConnectionPool *ConnectionPool::GetInstance()
{
	static ConnectionPool connPool;
	return &connPool;
}

//构造初始化
void ConnectionPool::init(string _url, string _user, string _passWord, string _dataBaseName, int _port, int _maxConn)
{
	url = _url;
	port = _port;
	user = _user;
	passWord = _passWord;
	dataBaseName = _dataBaseName;

	//初始化数据库连接池
	for (int i = 0; i < _maxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		con = mysql_real_connect(con, _url.c_str(), _user.c_str(),
								 _passWord.c_str(), _dataBaseName.c_str(), _port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);
		++freeConn;
	}

	reserve = Sem(freeConn);
	maxConn = freeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *ConnectionPool::GetConnection()
{
	MYSQL *con = NULL;

	if (connList.empty())
		return NULL;

	reserve.wait(); //信号量减1

	mutex.lock();

	con = connList.front(); //获取连接
	connList.pop_front();

	--freeConn;
	++curConn;

	mutex.unlock();
	return con;
}

//释放当前使用的连接
bool ConnectionPool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	mutex.lock();

	connList.push_back(con); //归还连接池
	++freeConn;
	--curConn;

	mutex.unlock();

	reserve.post(); //信号量加1
	return true;
}

//销毁数据库连接池
void ConnectionPool::DestroyPool()
{

	mutex.lock();
	if (!connList.empty())
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con); //销毁连接
		}
		curConn = 0;
		freeConn = 0;
		connList.clear(); //清空容器
	}

	mutex.unlock();
}

//当前空闲的连接数
int ConnectionPool::GetFreeConn()
{
	return this->freeConn;
}

ConnectionPool::~ConnectionPool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, ConnectionPool *connPool)
{
	*SQL = connPool->GetConnection();

	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
	poolRAII->ReleaseConnection(conRAII);
}