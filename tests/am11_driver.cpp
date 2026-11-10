// Real menu/confirmation code. No real profiles or messages.
#include "menu/Menu.h"
#include "menu/Commands/Menu/CmdOpenMenu.h"
#include "Utils/Confirm.h"
#include <iostream>
#include <memory>

namespace {
class Action : public astra_mail::ICommand {
public:
    std::string title() const override { return "Synthetic action"; }
    astra_mail::CommandResult execute() override {
        std::cout << "ACTION_EXECUTED\n";
        return astra_mail::CommandResult::Continue;
    }
};
}
int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "menu";
    if (mode == "confirm") {
        const bool accepted = astra_mail::confirm("Synthetic confirmation");
        std::cout << "CONFIRMED=" << accepted << "\n";
    } else {
        astra_mail::Menu root("Synthetic root", true);
        if (mode == "nested") {
            root.add(std::make_unique<astra_mail::CmdOpenMenu>([] {
                auto child = std::make_unique<astra_mail::Menu>("Synthetic child");
                child->add(std::make_unique<Action>());
                return child;
            }, "Synthetic child"));
        } else root.add(std::make_unique<Action>());
        if (mode == "bad") std::cin.setstate(std::ios::badbit);
        root.run();
    }
    std::cout << "RETURNED EOF=" << std::cin.eof() << " BAD=" << std::cin.bad() << "\n";
    return 0;
}
