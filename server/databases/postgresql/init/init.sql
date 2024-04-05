DO $$ BEGIN
    IF NOT EXISTS (SELECT FROM pg_database WHERE datname = 'chat') THEN
        CREATE DATABASE chat;
    END IF;
END $$;

\c chat;

CREATE TABLE public.users (
    id SERIAL PRIMARY KEY,
    password VARCHAR(255) NOT NULL,
    login VARCHAR(255) NOT NULL UNIQUE,
    bio VARCHAR(255),
    username VARCHAR(20) NOT NULL
);

-- INSERT INTO public.users (first_name, second_name, balance_USD, balance_RUB, login, password)
-- VALUES 
--     ('John', 'Doe', 1000, 50000, 'john_doe', 'password123'),
--     ('Alice', 'Smith', 500, 25000, 'alice_smith', 'securepass');
