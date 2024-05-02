DO $$ BEGIN
    IF NOT EXISTS (SELECT FROM pg_database WHERE datname = 'chat') THEN
        CREATE DATABASE chat;
    END IF;
END $$;

\c chat;

CREATE TABLE IF NOT EXISTS public.users (
    id SERIAL PRIMARY KEY,
    password VARCHAR(255) NOT NULL,
    login VARCHAR(255) NOT NULL UNIQUE,
    bio VARCHAR(255),
    username VARCHAR(20) NOT NULL
);