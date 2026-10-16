-- Creates the development user and database in the local PostgreSQL.
--   sudo -u postgres psql -f scripts/create-db.sql
-- The tables are created by the backend migrations at startup.
CREATE ROLE guita LOGIN PASSWORD 'guita';
CREATE DATABASE guita OWNER guita ENCODING 'UTF8' TEMPLATE template0;
