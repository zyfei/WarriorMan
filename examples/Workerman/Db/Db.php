<?php
namespace sama\db;

use sama\Sama;
use sama\App;

/**
 * 数据里操作了类
 */
class Db {
	
	//是否自动补全created_at等
	protected static $auto_datetime = false;

	protected static $created_at = "created_at";

	protected static $updated_at = "updated_at";

	protected static $deleted_at = "deleted_at";
	
	//是否使用软删除
	protected static $softDelete = false;

	/**
	 * 添加
	 */
	public static function add(&$arr = null) {
		if ($arr == null || count($arr) == 0) {
			return false;
		}
		$tm = date("Y-m-d H:i:s", time());
		// 写入创建时间和更新时间
		$arr[self::$created_at] = $tm;
		$arr[self::$updated_at] = $tm;
		
		$sql = 'insert into ' . static::$table . ' (';
		$sql_values = ' values (';
		$prepare_arr = array();
		foreach ($arr as $k => $n) {
			$sql = $sql . $k . ",";
			$sql_values = $sql_values . "?,";
			$prepare_arr[] = $n;
		}
		$sql = substr($sql, 0, strlen($sql) - 1);
		$sql_values = substr($sql_values, 0, strlen($sql_values) - 1);
		$sql = $sql . ") " . $sql_values . ")";
		$mysql = self::getDb();
		$stmt = $mysql->prepare($sql);
		$ret = $stmt->execute($prepare_arr);
		$arr["id"] = $mysql->insert_id;
		return $arr;
	}

	/**
	 * 修改
	 * $id可以不传，默认采用$arr里面的id
	 */
	public static function update(&$arr = null, $id = null) {
		if ($arr == null || count($arr) == 0) {
			return false;
		}
		if (! isset($arr["id"])) {
			return false;
		}
		if ($id == null) {
			$id = $arr["id"];
		}
		// 设置更新时间
		$tm = date("Y-m-d H:i:s", time());
		$arr[self::$updated_at] = $tm;
		
		if (array_key_exists(self::$deleted_at, $arr)) {
			if ($arr[self::$deleted_at] == null) {
				unset($arr[self::$deleted_at]);
			}
		}
		$sql = 'update ' . static::$table . ' set ';
		$prepare_arr = array();
		foreach ($arr as $k => $n) {
			$sql = $sql . $k . "=?,";
			$prepare_arr[] = $n;
		}
		$sql = substr($sql, 0, strlen($sql) - 1);
		$sql = $sql . " where id=?";
		$prepare_arr[] = $id;
		$mysql = self::getDb();
		$stmt = $mysql->prepare($sql);
		$ret = $stmt->execute($prepare_arr);
		return $arr;
	}

	/**
	 * 批量删除
	 */
	public static function delete_all($where_str = null, $prepare_arr = array()) {
		if (! is_array($prepare_arr)) {
			$prepare_arr = array(
				$prepare_arr
			);
		}
		// 如果是软删除
		if (static::$softDelete) {
			$tm = date("Y-m-d H:i:s", time());
			$sql = 'update ' . static::$table . ' set ' . self::$deleted_at . " ='" . $tm . "' where " . self::$deleted_at . " is null and " . $where_str;
		} else { // 如果是真删除
			$sql = "delete from `" . static::$table . "` where " . $where_str;
		}
		$mysql = self::getDb();
		$stmt = $mysql->prepare($sql);
		$ret = $stmt->execute($prepare_arr);
		return $ret;
	}

	/**
	 * 删除 传入id删除
	 */
	public static function delete($id = null) {
		if ($id == null) {
			return false;
		}
		if (static::$softDelete) {
			$tm = date("Y-m-d H:i:s", time());
			$model = array();
			$model["id"] = $id;
			$model[self::$deleted_at] = $tm;
			return self::update($model);
		}
		$mysql = self::getDb();
		$stmt = $mysql->prepare("delete from `" . static::$table . "` where id=?");
		$ret = $stmt->execute(array(
			$id
		));
		return $ret;
	}

	/**
	 * 通过id查
	 */
	public static function get($id = null) {
		$where = " id=? ";
		if (static::$softDelete) {
			$where = "$where and " . self::$deleted_at . " is null";
		}
		$prepare_arr = array(
			$id
		);
		$mysql = self::getDb();
		$stmt = $mysql->prepare("select * from `" . static::$table . "` where $where limit 1");
		$ret = $stmt->execute($prepare_arr)[0];
		return $ret;
	}

	/**
	 * 查询全部
	 */
	public static function all($where = "1=1", $order = "id desc", $prepare_arr = array()) {
		if (! is_array($prepare_arr)) {
			$prepare_arr = array(
				$prepare_arr
			);
		}
		if (static::$softDelete) {
			$where = "$where and " . self::$deleted_at . " is null";
		}
		$mysql = self::getDb();
		$stmt = $mysql->prepare("select * FROM `" . static::$table . "` where $where order by $order");
		$ret = $stmt->execute($prepare_arr);
		return $ret;
	}

	/**
	 * 查询全部
	 */
	public static function count($where = "1=1", $prepare_arr = array()) {
		if (! is_array($prepare_arr)) {
			$prepare_arr = array(
				$prepare_arr
			);
		}
		if (static::$softDelete) {
			$where = "$where and " . self::$deleted_at . " is null";
		}
		$mysql = self::getDb();
		$stmt = $mysql->prepare("select count(*) as c  FROM `" . static::$table . "` where $where limit 1");
		$ret = $stmt->execute($prepare_arr)[0];
		return $ret["c"];
	}

	/**
	 * 分页查询
	 */
	public static function page($index = 0, $length = 0, $where = " 1=1 ", $order = "id desc", $prepare_arr = array()) {
		if (! is_array($prepare_arr)) {
			$prepare_arr = array(
				$prepare_arr
			);
		}
		if (static::$softDelete) {
			$where = "$where and " . self::$deleted_at . " is null";
		}
		$sql = "select * FROM `" . static::$table . "` where $where order by $order LIMIT $length OFFSET $index";
		$mysql = self::getDb();
		$stmt = $mysql->prepare($sql);
		$arr["list"] = $stmt->execute($prepare_arr);
		
		$sql = "select count(*) as c  FROM `" . static::$table . "` where $where limit 1";
		$stmt = $mysql->prepare($sql);
		$allCount = $stmt->execute($prepare_arr)[0]["c"];
		$arr["all_count"] = $allCount;
		if ($length != 0) {
			$yu = 0;
			if ($allCount > $length) {
				if ($allCount % $length > 0) {
					$yu = $allCount / $length + 1;
				} else {
					$yu = $allCount / $length;
				}
			} else {
				$yu = 1;
			}
			$arr["all_page_num"] = (int) $yu;
		} else {
			$arr["all_page_num"] = 1;
		}
		return $arr;
	}

	/**
	 * 获取当前协程使用的Db实例
	 * 和当前使用的App绑定在一起
	 */
	public static function getDb() {
		return App::getApp()->get_db();
	}

	/**
	 * 如果没找到静态方法，那么直接使用成员方法
	 */
	public static function __callStatic($method, $arg) {
		return self::getDb()->$method(...$arg);
	}
}