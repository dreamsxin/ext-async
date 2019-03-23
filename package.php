<?php

// Helper script being used to sync package.xml file list.

define('NS', 'http://pear.php.net/dtd/package-2.0');

function get_role($file)
{
    switch (strtolower(pathinfo($file, PATHINFO_EXTENSION))) {
        case 'md':
            return 'doc';
    }

    return 'src';
}

function collect_files($dir, array $include, $role = null)
{
    $result = [];

    foreach (glob($dir . '/*') as $entry) {
        if (is_dir($entry)) {
            $result = array_merge($result, collect_files($entry, $include, $role));
        } elseif (in_array(strtolower(basename($entry)), $include)) {
            $result[] = [
                $entry,
                $role ?? get_role($entry)
            ];
        } elseif (in_array(strtolower(pathinfo($entry, PATHINFO_EXTENSION)), $include)) {
            $result[] = [
                $entry,
                $role ?? get_role($entry)
            ];
        }
    }

    return $result;
}

$file = __DIR__ . '/package.xml';

$dom = new \DOMDocument();
$dom->formatOutput = true;
$dom->preserveWhiteSpace = false;

$dom->load($file);

$xpath = new \DOMXPath($dom);
$xpath->registerNamespace('p', NS);

foreach ($xpath->query('/p:package/p:date') as $el) {
    foreach ($el->childNodes as $child) {
        $el->removeChild($child);
    }

    $el->appendChild($dom->createTextNode(gmdate('Y-m-d')));
}

foreach ($xpath->query('/p:package/p:time') as $el) {
    foreach ($el->childNodes as $child) {
        $el->removeChild($child);
    }

    $el->appendChild($dom->createTextNode(gmdate('H:i:s')));
}

foreach ($xpath->query('/p:package/p:contents') as $contents) {
    foreach ($contents->childNodes as $child) {
        $contents->removeChild($child);
    }

    $dir = $contents->appendChild($dom->createElementNS(NS, 'dir'));

    $name = $dom->createAttribute('name');
    $name->appendChild($dom->createTextNode('/'));
    $dir->appendChild($name);

    $files = [];

    foreach (glob(__DIR__ . '/*') as $entry) {
        $include = false;

        switch (basename($entry)) {
            case 'LICENSE':
            case 'README.md':
                $include = true;
                $role = 'doc';
                break;
            case 'config.m4':
            case 'config.w32':

            case 'Makefile.frag':
            case 'php_async.c':
            case 'php_async.h':
                $role = 'src';
                $include = true;
                break;
        }

        if (!$include) {
            continue;
        }

        if (is_dir($entry)) {
            continue;
        }

        $files[] = [
            $entry,
            $role
        ];
    }

    $files = array_merge($files, collect_files(__DIR__ . '/include', [
        'h'
    ]));

    $files = array_merge($files, collect_files(__DIR__ . '/src', [
        'c'
    ]));

    $files = array_merge($files, collect_files(__DIR__ . '/thirdparty/boost', [
        'license',
        's'
    ]));

    $files = array_merge($files, collect_files(__DIR__ . '/thirdparty/libuv', [
        'h',
        'c',
        'm4',
        'sh',
        'ac',
        'in',
        'md',
        'am',
        'license'
    ]));
    
    $files = array_merge($files, collect_files(__DIR__ . '/tests', [
        'phpt',
        'php',
        'inc'
    ], 'test'));

    foreach ($files as list ($entry, $role)) {
        $fe = $dom->createElementNS(NS, 'file');
        $dir->appendChild($fe);

        $a = $dom->createAttribute('role');
        $a->appendChild($dom->createTextNode($role));
        $fe->appendChild($a);

        $a = $dom->createAttribute('name');
        $a->appendChild($dom->createTextNode(substr($entry, strlen(__DIR__) + 1)));
        $fe->appendChild($a);
    }
}

$dom->save($file);
