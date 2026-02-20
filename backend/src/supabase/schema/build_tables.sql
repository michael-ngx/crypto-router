-- Enable UUID extension if not already enabled
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Create enum types in public schema
CREATE TYPE public.order_side AS ENUM ('buy', 'sell');
CREATE TYPE public.order_type AS ENUM ('market', 'limit');
CREATE TYPE public.order_status AS ENUM ('open', 'executing', 'partially_filled', 'filled', 'failed', 'cancelled', 'expired');
CREATE TYPE public.leg_status AS ENUM ('planned', 'submitted', 'acknowledged', 'filled', 'failed', 'cancelled', 'expired', 'rejected');

-- 1. Users table in public schema
CREATE TABLE IF NOT EXISTS public.users (
  id uuid NOT NULL DEFAULT uuid_generate_v4(),
  first_name character varying NOT NULL,
  last_name character varying NOT NULL,
  email character varying NOT NULL UNIQUE,
  password character varying NOT NULL,
  CONSTRAINT users_pkey PRIMARY KEY (id)
);

-- 2. Orders table in public schema
CREATE TABLE IF NOT EXISTS public.orders (
  id uuid NOT NULL DEFAULT uuid_generate_v4(),
  user_id uuid NOT NULL,
  symbol character varying NOT NULL,
  side public.order_side NOT NULL,
  order_type public.order_type NOT NULL,
  quantity_requested numeric NOT NULL,
  limit_price numeric,
  quantity_planned numeric NOT NULL,
  price_planned_avg numeric NOT NULL,
  fully_routable boolean NOT NULL,
  routing_message text,
  quantity_filled numeric NOT NULL DEFAULT '0'::numeric,
  price_filled_avg numeric,
  status public.order_status NOT NULL DEFAULT 'open'::public.order_status,
  failure_code text,
  failure_message text,
  created_at timestamp with time zone NOT NULL DEFAULT now(),
  execution_started_at timestamp with time zone,
  terminal_at timestamp with time zone,
  last_updated_at timestamp with time zone NOT NULL DEFAULT now(),
  CONSTRAINT orders_pkey PRIMARY KEY (id),
  CONSTRAINT orders_user_id_fkey FOREIGN KEY (user_id) REFERENCES public.users(id)
);

-- 3. Order Legs table in public schema (tracks order execution across exchanges, determined by router)
CREATE TABLE IF NOT EXISTS public.order_legs (
  id uuid NOT NULL DEFAULT uuid_generate_v4(),
  order_id uuid NOT NULL,
  venue character varying NOT NULL,
  status public.leg_status NOT NULL DEFAULT 'planned'::public.leg_status,
  quantity_planned numeric NOT NULL,
  limit_price numeric,
  price_planned numeric NOT NULL,
  quantity_submitted numeric,
  price_submitted numeric,
  quantity_filled numeric NOT NULL DEFAULT '0'::numeric,
  price_filled_avg numeric,
  client_order_id character varying,
  venue_order_id character varying,
  error_code text,
  error_message text,
  created_at timestamp with time zone NOT NULL DEFAULT now(),
  submitted_at timestamp with time zone,
  acknowledged_at timestamp with time zone,
  first_fill_at timestamp with time zone,
  last_fill_at timestamp with time zone,
  terminal_at timestamp with time zone,
  last_updated_at timestamp with time zone NOT NULL DEFAULT now(),
  CONSTRAINT order_legs_pkey PRIMARY KEY (id),
  CONSTRAINT order_legs_order_id_fkey FOREIGN KEY (order_id) REFERENCES public.orders(id)
);

-- Create indexes for better query performance
CREATE INDEX IF NOT EXISTS idx_orders_user_id ON public.orders(user_id);
CREATE INDEX IF NOT EXISTS idx_orders_status ON public.orders(status);
CREATE INDEX IF NOT EXISTS idx_orders_created_at ON public.orders(created_at);
CREATE INDEX IF NOT EXISTS idx_order_legs_order_id ON public.order_legs(order_id);
CREATE INDEX IF NOT EXISTS idx_order_legs_venue ON public.order_legs(venue);
CREATE INDEX IF NOT EXISTS idx_users_email ON public.users(email);
