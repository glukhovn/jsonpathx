CREATE EXTENSION jsonpathx;

SELECT jsonpath_embed_vars('$.a + $a', '"aaa"');
SELECT jsonpath_embed_vars('$.a + $a', '{"b": "abc"}');
SELECT jsonpath_embed_vars('$.a + $a', '{"a": "abc"}');
SELECT jsonpath_embed_vars('$.a + $a.double()', '{"a": "abc"}');
SELECT jsonpath_embed_vars('$.a + $a.x.double()', '{"a": {"x": -12.34}}');
SELECT jsonpath_embed_vars('$[*] ? (@ > $min && @ <= $max)', '{"min": -1.23, "max": 5.0}');
SELECT jsonpath_embed_vars(jsonpath_embed_vars('$[*] ? (@ > $min && @ <= $max)', '{"min": -1.23}'), '{"max": 5.0}');

DROP EXTENSION jsonpathx;
