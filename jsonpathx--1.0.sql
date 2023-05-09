/* contrib/jsonpathx/jsonpathx--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION jsonpathx" to load this file. \quit

CREATE FUNCTION jsonpath_embed_vars(jsp jsonpath, vars jsonb) RETURNS jsonpath
LANGUAGE C STRICT IMMUTABLE
AS 'MODULE_PATHNAME';

COMMENT ON FUNCTION jsonpath_embed_vars IS 'embed variable values inside jsonpath';
