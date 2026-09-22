#include "antsql/table_store_executor.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>

namespace {
using namespace antsql;

QueryRequest Insert(const std::string& sql, std::uint32_t key) {
  return {sql, {QueryKind::Insert, "users", key, false}, {}, 16};
}

QueryRequest Select(std::uint32_t key) {
  return {"SELECT * FROM users WHERE id = " + std::to_string(key),
          {QueryKind::Select, "users", key, false}, {}, 16};
}

void TableStoreExecutorTests() {
  TableStoreExecutor store;

  const auto inserted =
      store.Execute(Insert("INSERT INTO users (id, name, balance) VALUES (7, 'ada', 100)", 7));
  assert(inserted.status == QueryStatus::Ok);
  assert(store.RowCount("users") == 1);

  const auto selected = store.Execute(Select(7));
  assert(selected.status == QueryStatus::Ok);
  assert(selected.payload == "balance=100,id=7,name=ada");

  const auto missing = store.Execute(Select(9));
  assert(missing.status == QueryStatus::Ok);
  assert(missing.payload.empty());

  QueryRequest update{"UPDATE users SET balance = 250 WHERE id = 7",
                       {QueryKind::Update, "users", 7, false}, {}, 16};
  const auto updated = store.Execute(update);
  assert(updated.status == QueryStatus::Ok);
  assert(store.DumpRow("users", 7) == "balance=250,id=7,name=ada");

  QueryRequest update_missing{"UPDATE users SET balance = 1 WHERE id = 9",
                              {QueryKind::Update, "users", 9, false}, {}, 16};
  const auto update_missing_response = store.Execute(update_missing);
  assert(update_missing_response.status == QueryStatus::ExecutionError);

  QueryRequest del{"DELETE FROM users WHERE id = 7", {QueryKind::Delete, "users", 7, false}, {}, 16};
  const auto deleted = store.Execute(del);
  assert(deleted.status == QueryStatus::Ok);
  assert(store.RowCount("users") == 0);
}

void TableStoreExecutorWalReplayTests() {
  const std::string wal_path = "table_store_executor_test.wal";
  std::filesystem::remove(wal_path);
  {
    TableStoreExecutor store(wal_path);
    store.Execute(Insert("INSERT INTO users (id, name) VALUES (1, 'grace')", 1));
    QueryRequest update{"UPDATE users SET name = 'grace_h' WHERE id = 1",
                        {QueryKind::Update, "users", 1, false}, {}, 16};
    store.Execute(update);
  }
  {
    // A fresh executor over the same WAL file should recover the mutations
    // made by the previous one, since durability across a node restart is
    // the whole point of logging writes rather than keeping them purely
    // in memory.
    TableStoreExecutor recovered(wal_path);
    assert(recovered.DumpRow("users", 1) == "id=1,name=grace_h");
  }
  std::filesystem::remove(wal_path);
}

struct TableStoreExecutorTestRegistration {
  TableStoreExecutorTestRegistration() {
    TableStoreExecutorTests();
    TableStoreExecutorWalReplayTests();
  }
} table_store_executor_test_registration;
}  // namespace
