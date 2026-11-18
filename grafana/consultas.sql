-- ============================================================
-- Bueiro Inteligente - consultas dos painéis do Grafana
-- Data source: InfluxDB Cloud Serverless (linguagem SQL)
-- Bucket: bueiro_inteligente | Measurements: leituras, limpezas
-- O dashboard completo pode ser importado de grafana/dashboard_bueiro.json
-- ============================================================


-- ------------------------------------------------------------
-- Painel: O que precisa ser feito agora
-- ------------------------------------------------------------
WITH ult AS (
  SELECT id_bueiro, endereco, volume_l, capacidade_l, ocupacao_pct, status, time,
         ROW_NUMBER() OVER (PARTITION BY id_bueiro ORDER BY time DESC) AS ordem
  FROM "leituras"
  WHERE time >= $__timeFrom AND time <= $__timeTo AND volume_l IS NOT NULL
),
lim AS (
  SELECT id_bueiro, resultado,
         ROW_NUMBER() OVER (PARTITION BY id_bueiro ORDER BY time DESC) AS ordem
  FROM "limpezas"
  WHERE time >= $__timeFrom AND time <= $__timeTo
),
base AS (
  SELECT u.id_bueiro, u.endereco, u.volume_l, u.capacidade_l, u.ocupacao_pct, u.status, l.resultado,
         (CAST(now() AS BIGINT) - CAST(u.time AS BIGINT)) / 1000000000 AS seg
  FROM ult u
  LEFT JOIN lim l ON u.id_bueiro = l.id_bueiro AND l.ordem = 1
  WHERE u.ordem = 1
),
classif AS (
  SELECT *,
         CASE WHEN status = 'vermelho' AND resultado = 'nao_resolvida' THEN 1
              WHEN seg > 30 THEN 2
              WHEN status = 'vermelho' THEN 3
              WHEN status = 'amarelo' THEN 4
              ELSE 5 END AS prioridade
  FROM base
)
SELECT prioridade AS "Prioridade",
       id_bueiro  AS "Bueiro",
       endereco   AS "Endereço",
       CASE prioridade
            WHEN 1 THEN 'Grade não resolveu'
            WHEN 2 THEN 'Sem comunicação'
            WHEN 3 THEN 'Crítico: grade em limpeza'
            WHEN 4 THEN 'Atenção'
            ELSE 'Normal' END AS "Situação",
       CASE prioridade
            WHEN 1 THEN 'Enviar equipe para limpeza manual'
            WHEN 2 THEN 'Verificar o dispositivo no local'
            WHEN 3 THEN 'Aguardar verificação automática (15 s)'
            WHEN 4 THEN 'Acompanhar; programar limpeza se continuar subindo'
            ELSE 'Nenhuma ação' END AS "O que fazer",
       ocupacao_pct AS "Ocupação (%)",
       volume_l     AS "Resíduos (L)",
       CASE WHEN capacidade_l * 0.75 - volume_l > 0 THEN capacidade_l * 0.75 - volume_l ELSE 0 END AS "Falta até o crítico (L)",
       CASE WHEN seg < 0 THEN 0 ELSE seg END AS "Última leitura há"
FROM classif
ORDER BY prioridade, id_bueiro;


-- ------------------------------------------------------------
-- Painel: Mapa dos bueiros
-- ------------------------------------------------------------
WITH ultimas AS (
  SELECT id_bueiro, endereco, latitude, longitude, volume_l, ocupacao_pct,
         ROW_NUMBER() OVER (PARTITION BY id_bueiro ORDER BY time DESC) AS ordem
  FROM "leituras"
  WHERE time >= $__timeFrom AND time <= $__timeTo AND volume_l IS NOT NULL
)
SELECT id_bueiro    AS "Bueiro",
       endereco     AS "Endereço",
       latitude,
       longitude,
       volume_l     AS "Resíduos (L)",
       ocupacao_pct AS "Ocupação (%)"
FROM ultimas
WHERE ordem = 1;


-- ------------------------------------------------------------
-- Painel: Histórico da ocupação por bueiro (%)
-- ------------------------------------------------------------
SELECT time, id_bueiro, ocupacao_pct
FROM "leituras"
WHERE time >= $__timeFrom AND time <= $__timeTo AND volume_l IS NOT NULL
ORDER BY time;


-- ------------------------------------------------------------
-- Painel: Eficácia da grade autolimpante por bueiro
-- ------------------------------------------------------------
SELECT id_bueiro AS "Bueiro",
       SUM(CASE WHEN resultado = 'resolvida'     THEN 1 ELSE 0 END) AS "Resolvidas pela grade",
       SUM(CASE WHEN resultado = 'nao_resolvida' THEN 1 ELSE 0 END) AS "Encaminhadas à equipe"
FROM "limpezas"
WHERE time >= $__timeFrom AND time <= $__timeTo
GROUP BY id_bueiro
ORDER BY id_bueiro;


-- ------------------------------------------------------------
-- Painel: Últimas limpezas automáticas
-- ------------------------------------------------------------
SELECT time AS "Horário",
       id_bueiro AS "Bueiro",
       CASE WHEN resultado = 'resolvida' THEN 'Resolvida pela grade'
            ELSE 'Encaminhada à equipe' END AS "Resultado da limpeza",
       volume_l AS "Resíduos após a limpeza (L)",
       ocupacao_pct AS "Ocupação após a limpeza (%)"
FROM "limpezas"
WHERE time >= $__timeFrom AND time <= $__timeTo
ORDER BY time DESC
LIMIT 10;
