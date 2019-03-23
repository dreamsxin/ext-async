--TEST--
Filesystem provides access to directory contents.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$dh = opendir(__DIR__ . '/assets/dir');
$meta = stream_get_meta_data($dh);

var_dump($meta['wrapper_type'], $meta['stream_type']);

$entries = [];
$copy = [];

while (false !== ($entry = readdir($dh))) {
    $entries[] = $entry;
}

fseek($dh, 1, SEEK_END);
fseek($dh, -20, SEEK_END);
fseek($dh, 5, SEEK_CUR);
fseek($dh, 3);
fseek($dh, 30);

rewinddir($dh);

fseek($dh, -60, SEEK_CUR);
fseek($dh, 2, SEEK_CUR);

rewinddir($dh);

while (false !== ($entry = readdir($dh))) {
    $copy[] = $entry;
}

closedir($dh);

sort($entries);
sort($copy);

print_r($entries);
var_dump($entries == $copy);

--EXPECT--
string(15) "plainfile/async"
string(9) "dir/async"
Array
(
    [0] => .
    [1] => ..
    [2] => bar.txt
    [3] => foo.txt
    [4] => sub
)
bool(true)
