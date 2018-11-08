<?php

$dh = opendir(__DIR__);

print_r(stream_get_meta_data($dh));

try {
    while (false !== ($entry = readdir($dh))) {
        var_dump($entry);
    }
} finally {
    closedir($dh);
}

$fp = fopen(__DIR__ . '/dummy.txt', 'rb');

print_r(stream_get_meta_data($fp));
print_r(array_filter(fstat($fp), 'is_string', ARRAY_FILTER_USE_KEY));

var_dump(feof($fp));
var_dump(fseek($fp, -10, SEEK_END));
var_dump(ftell($fp));
var_dump(rewind($fp));
var_dump(fread($fp, 5));
var_dump(fread($fp, 10));
var_dump(feof($fp));

var_dump(require __DIR__ . '/bfile.php');

$file = __DIR__ . '/dummy2.txt';

if (touch($file)) {
    var_dump(filemtime($file));
}

$fp = fopen($file, 'w');

var_dump(flock($fp, LOCK_EX));
var_dump(fwrite($fp, "Some more data...\n"));
var_dump(fwrite($fp, "DONE\n\n"));
fclose($fp);

print_r(array_filter(stat(__FILE__), 'is_string', ARRAY_FILTER_USE_KEY));
var_dump(file_get_contents($file));

var_dump(unlink('file://' . $file));

