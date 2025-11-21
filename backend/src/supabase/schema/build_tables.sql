-- Enable UUID extension if not already enabled
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Create enum types in public schema
CREATE TYPE public.order_side AS ENUM ('buy', 'sell');
CREATE TYPE public.order_type AS ENUM ('market', 'limit');
CREATE TYPE public.order_status AS ENUM ('open', 'partially_filled', 'filled', 'cancelled');

-- 1. Users table in public schema
CREATE TABLE IF NOT EXISTS public.users (
  id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  first_name VARCHAR(255) NOT NULL,
  last_name VARCHAR(255) NOT NULL,
  email VARCHAR(255) UNIQUE NOT NULL,
  password VARCHAR(255) NOT NULL -- Should be hashed before storage
);

-- 2. Orders table in public schema
CREATE TABLE IF NOT EXISTS public.orders (
  id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  user_id UUID NOT NULL REFERENCES public.users(id) ON DELETE CASCADE,
  side public.order_side NOT NULL,
  order_type public.order_type NOT NULL,
  average_fill_price DECIMAL(20,8),
  status public.order_status NOT NULL DEFAULT 'open',
  created_at TIMESTAMPTZ DEFAULT NOW(),
  closed_at TIMESTAMPTZ
);

-- 3. Order breakdown table in public schema (tracks order execution across exchanges)
CREATE TABLE IF NOT EXISTS public.order_breakdown (
  id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  order_id UUID NOT NULL REFERENCES public.orders(id) ON DELETE CASCADE,
  exchange_name VARCHAR(100) NOT NULL,
  quantity_filled DECIMAL(20,8) NOT NULL,
  fill_price DECIMAL(20,8) NOT NULL,
  filled_at TIMESTAMPTZ DEFAULT NOW(),
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Create indexes for better query performance
CREATE INDEX IF NOT EXISTS idx_orders_user_id ON public.orders(user_id);
CREATE INDEX IF NOT EXISTS idx_orders_status ON public.orders(status);
CREATE INDEX IF NOT EXISTS idx_orders_created_at ON public.orders(created_at);
CREATE INDEX IF NOT EXISTS idx_order_breakdown_order_id ON public.order_breakdown(order_id);
CREATE INDEX IF NOT EXISTS idx_order_breakdown_exchange ON public.order_breakdown(exchange_name);
CREATE INDEX IF NOT EXISTS idx_users_email ON public.users(email);