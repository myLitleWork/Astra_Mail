// Verify the exact sample shipped with the release using the real importer.
#include "core/contacts/CsvContactsImporter.h"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    astra_mail::CsvContactsImporter importer;
    if (!importer.loadFromFile(argv[1]) || importer.skippedCount() || importer.contacts().size() != 3) return 1;
    const auto& c = importer.contacts();
    if (c.at("demo_office").email != "office@example.invalid" ||
        c.at("demo_duty").email != "duty@example.invalid" ||
        c.at("demo_duty").displayName != "Дежурный, пример" ||
        c.at("demo_team").email != "team@example.invalid") return 1;
    std::cout << "CSV_SAMPLE_OK=1 CONTACTS=3 SKIPPED=0\n";
}
