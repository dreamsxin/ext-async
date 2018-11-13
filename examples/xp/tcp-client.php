<?php

error_reporting(-1);

$errno = null;
$errstr = null;

$fp = stream_socket_client('tcp://127.0.0.1:10008', $errno, $errstr, 1, STREAM_CLIENT_CONNECT);

print_r(stream_get_meta_data($fp));

var_dump(stream_socket_get_name($fp, true));

var_dump(fwrite($fp, 'Hello World :)'));
var_dump(stream_socket_shutdown($fp, STREAM_SHUT_WR));
var_dump(fread($fp, 8192));

fclose($fp);
