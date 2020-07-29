<?php
namespace Workerman\Db;

/**
 * mysql连接池
 */
class MysqlPool extends AbstractPool
{
    protected function createDb()
    {
        $db = new Mysql($this->config['host'], $this->config['port'], $this->config['username'], $this->config['password'], $this->config['db_name']);
        // if ($db->connected == false) {
        // dd("createDb error : " . $db->connect_error);
        // return false;
        // }
        return $db;
    }

    protected function checkDb($db)
    {
        return true;
        // return $db->connected;
    }
    
    protected function closeDb($db)
    {
        $db->closeConnection();
        // return $db->connected;
    }
}